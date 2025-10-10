#include "usb_msc.h"
#include <furi_hal_usb.h>
#include <usb.h>
#include <usbd_core.h>
#include <string.h>

#define TAG "UsbMsc"

// Thread events
typedef enum {
    EventExit = (1 << 0),
    EventReset = (1 << 1),
    EventRxTx = (1 << 2),
} WorkerEventFlag;

typedef enum {
    MSC_STATE_IDLE,
    MSC_STATE_READ_CBW,
    MSC_STATE_DATA_IN,
    MSC_STATE_DATA_OUT,
    MSC_STATE_BUILD_CSW,
    MSC_STATE_WRITE_CSW,
} MscState;

struct UsbMscContext {
    UsbScsiContext* scsi;
    usbd_device* usb_dev;

    MscState state;
    UsbMscCbw cbw;
    UsbMscCsw csw;

    uint8_t tx_buffer[512];
    uint32_t tx_len; // Length of data in tx_buffer (0 = buffer empty)
    uint32_t tx_offset; // Total bytes sent so far

    uint8_t rx_buffer[512];
    uint32_t rx_len;

    bool active;

    // Worker thread
    FuriThread* thread;
    FuriThreadId thread_id;

    // Previous USB mode for restoration
    FuriHalUsbInterface* prev_usb_mode;
};

// USB MSC class-specific requests
#define USB_MSC_BOT_GET_MAX_LUN 0xFE
#define USB_MSC_BOT_RESET       0xFF

// Forward declarations
static usbd_respond
    usb_msc_control(usbd_device* dev, usbd_ctlreq* req, usbd_rqc_callback* callback)
        __attribute__((unused));
static usbd_respond usb_msc_config(usbd_device* dev, uint8_t cfg) __attribute__((unused));
static void usb_msc_ep_callback(usbd_device* dev, uint8_t event, uint8_t ep);

// USB descriptors
static struct usb_device_descriptor msc_device_desc = {
    .bLength = sizeof(struct usb_device_descriptor),
    .bDescriptorType = USB_DTYPE_DEVICE,
    .bcdUSB = VERSION_BCD(2, 0, 0),
    .bDeviceClass = USB_CLASS_PER_INTERFACE,
    .bDeviceSubClass = USB_SUBCLASS_NONE,
    .bDeviceProtocol = USB_PROTO_NONE,
    .bMaxPacketSize0 = 8,
    .idVendor = 0x0483,
    .idProduct = 0x5720, // Use STM's VID/PID for Mass Storage
    .bcdDevice = VERSION_BCD(1, 0, 0),
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

#pragma pack(push, 1)
struct msc_config_descriptor {
    struct usb_config_descriptor config;
    struct usb_interface_descriptor iface;
    struct usb_endpoint_descriptor ep_in;
    struct usb_endpoint_descriptor ep_out;
} __attribute__((packed));
#pragma pack(pop)

static struct msc_config_descriptor msc_cfg_desc = {
    .config =
        {
            .bLength = sizeof(struct usb_config_descriptor),
            .bDescriptorType = USB_DTYPE_CONFIGURATION,
            .wTotalLength = sizeof(struct msc_config_descriptor),
            .bNumInterfaces = 1,
            .bConfigurationValue = 1,
            .iConfiguration = 0,
            .bmAttributes = USB_CFG_ATTR_RESERVED | USB_CFG_ATTR_SELFPOWERED,
            .bMaxPower = 250, // 500mA
        },
    .iface =
        {
            .bLength = sizeof(struct usb_interface_descriptor),
            .bDescriptorType = USB_DTYPE_INTERFACE,
            .bInterfaceNumber = 0,
            .bAlternateSetting = 0,
            .bNumEndpoints = 2,
            .bInterfaceClass = USB_CLASS_MASS_STORAGE,
            .bInterfaceSubClass = 0x06, // SCSI transparent command set
            .bInterfaceProtocol = 0x50, // Bulk-Only Transport
            .iInterface = 0,
        },
    .ep_in =
        {
            .bLength = sizeof(struct usb_endpoint_descriptor),
            .bDescriptorType = USB_DTYPE_ENDPOINT,
            .bEndpointAddress = USB_MSC_EP_IN,
            .bmAttributes = USB_EPTYPE_BULK,
            .wMaxPacketSize = USB_MSC_EP_SIZE,
            .bInterval = 0,
        },
    .ep_out =
        {
            .bLength = sizeof(struct usb_endpoint_descriptor),
            .bDescriptorType = USB_DTYPE_ENDPOINT,
            .bEndpointAddress = USB_MSC_EP_OUT,
            .bmAttributes = USB_EPTYPE_BULK,
            .wMaxPacketSize = USB_MSC_EP_SIZE,
            .bInterval = 0,
        },
};

static const struct usb_string_descriptor msc_string_manuf = USB_STRING_DESC("boot2flipper");
static const struct usb_string_descriptor msc_string_product = USB_STRING_DESC("iPXE Boot Disk");
static const struct usb_string_descriptor msc_string_serial = USB_STRING_DESC("B2F00001");

// Global MSC context (needed for USB callbacks)
static UsbMscContext* g_msc_ctx = NULL;

UsbMscContext* usb_msc_alloc(void) {
    UsbMscContext* ctx = malloc(sizeof(UsbMscContext));
    memset(ctx, 0, sizeof(UsbMscContext));

    ctx->scsi = NULL;
    ctx->usb_dev = NULL;
    ctx->state = MSC_STATE_IDLE;
    ctx->active = false;
    ctx->prev_usb_mode = NULL;

    return ctx;
}

void usb_msc_free(UsbMscContext* ctx) {
    if(ctx == NULL) return;

    if(ctx->active) {
        usb_msc_stop(ctx);
    }

    free(ctx);
}

bool usb_msc_set_scsi(UsbMscContext* ctx, UsbScsiContext* scsi) {
    if(ctx == NULL || scsi == NULL) {
        return false;
    }

    ctx->scsi = scsi;
    return true;
}

static usbd_respond
    usb_msc_control(usbd_device* dev, usbd_ctlreq* req, usbd_rqc_callback* callback) {
    UNUSED(dev);
    UNUSED(callback);

    if((req->bmRequestType & (USB_REQ_TYPE | USB_REQ_RECIPIENT)) ==
       (USB_REQ_CLASS | USB_REQ_INTERFACE)) {
        switch(req->bRequest) {
        case USB_MSC_BOT_GET_MAX_LUN:
            // Return max LUN = 0 (only one LUN)
            req->data[0] = 0;
            return usbd_ack;

        case USB_MSC_BOT_RESET:
            // Reset MSC state
            if(g_msc_ctx && g_msc_ctx->thread_id != NULL) {
                furi_thread_flags_set(g_msc_ctx->thread_id, EventReset);
            }
            return usbd_ack;

        default:
            return usbd_fail;
        }
    }

    return usbd_fail;
}

static usbd_respond usb_msc_config(usbd_device* dev, uint8_t cfg) {
    if(cfg == 0) {
        // Deconfigure endpoints
        usbd_ep_deconfig(dev, USB_MSC_EP_IN);
        usbd_ep_deconfig(dev, USB_MSC_EP_OUT);
        usbd_reg_endpoint(dev, USB_MSC_EP_IN, NULL);
        usbd_reg_endpoint(dev, USB_MSC_EP_OUT, NULL);
        return usbd_ack;
    }

    // Configure endpoints
    usbd_ep_config(dev, USB_MSC_EP_IN, USB_EPTYPE_BULK, USB_MSC_EP_SIZE);
    usbd_ep_config(dev, USB_MSC_EP_OUT, USB_EPTYPE_BULK, USB_MSC_EP_SIZE);
    usbd_reg_endpoint(dev, USB_MSC_EP_IN, usb_msc_ep_callback);
    usbd_reg_endpoint(dev, USB_MSC_EP_OUT, usb_msc_ep_callback);

    return usbd_ack;
}

// Endpoint callbacks - only signal thread flags
static void usb_msc_ep_callback(usbd_device* dev, uint8_t event, uint8_t ep) {
    UNUSED(dev);
    UNUSED(event);
    UNUSED(ep);

    UsbMscContext* ctx = g_msc_ctx;
    if(ctx == NULL || ctx->thread_id == NULL) return;

    // Signal worker thread to process event
    furi_thread_flags_set(ctx->thread_id, EventRxTx);
}

// Worker thread function - processes USB MSC state machine
static int32_t mass_thread_worker(void* context) {
    UsbMscContext* ctx = (UsbMscContext*)context;

    FURI_LOG_I(TAG, "Worker thread started");

    // Store thread ID for callbacks
    ctx->thread_id = furi_thread_get_current_id();
    ctx->state = MSC_STATE_READ_CBW;

    while(1) {
        uint32_t flags = furi_thread_flags_wait(
            EventExit | EventReset | EventRxTx, FuriFlagWaitAny, FuriWaitForever);

        // Check for exit
        if(flags & EventExit) {
            FURI_LOG_I(TAG, "Worker thread exiting");
            break;
        }

        // Check for reset
        if(flags & EventReset) {
            FURI_LOG_D(TAG, "Reset event");
            ctx->state = MSC_STATE_READ_CBW;
            ctx->tx_offset = 0;
            ctx->tx_len = 0;
            continue;
        }

        // Process RX/TX event
        if(flags & EventRxTx) {
            usbd_device* dev = ctx->usb_dev;
            if(dev == NULL) continue;

            switch(ctx->state) {
            case MSC_STATE_READ_CBW: {
                // Read Command Block Wrapper from host
                int32_t len = usbd_ep_read(dev, USB_MSC_EP_OUT, &ctx->cbw, sizeof(UsbMscCbw));

                if(len <= 0) {
                    // No data available yet, wait for next RX event
                    break;
                }

                if(len != sizeof(UsbMscCbw) || ctx->cbw.dSignature != USB_MSC_CBW_SIGNATURE) {
                    FURI_LOG_E(TAG, "Invalid CBW: len=%ld, sig=0x%08lX", len, ctx->cbw.dSignature);
                    usbd_ep_stall(dev, USB_MSC_EP_IN);
                    usbd_ep_stall(dev, USB_MSC_EP_OUT);
                    ctx->state = MSC_STATE_READ_CBW;
                    break;
                }

                FURI_LOG_D(
                    TAG,
                    "CBW: cmd=0x%02X, datalen=%lu, flags=0x%02X, tag=%lu",
                    ctx->cbw.CB[0],
                    ctx->cbw.dDataLength,
                    ctx->cbw.bmFlags,
                    ctx->cbw.dTag);

                // Process SCSI command
                bool cmd_ok = usb_scsi_process_command(ctx->scsi, ctx->cbw.CB, ctx->cbw.bCBLength);

                if(!cmd_ok) {
                    FURI_LOG_W(TAG, "SCSI command failed");
                    ctx->csw.bStatus = USB_MSC_CSW_STATUS_FAILED;

                    // Send CSW immediately for failed commands
                    ctx->csw.dSignature = USB_MSC_CSW_SIGNATURE;
                    ctx->csw.dTag = ctx->cbw.dTag;
                    ctx->csw.dDataResidue = ctx->cbw.dDataLength;

                    usbd_ep_write(dev, USB_MSC_EP_IN, &ctx->csw, sizeof(UsbMscCsw));
                    FURI_LOG_D(TAG, "CSW sent (failed): status=%u", ctx->csw.bStatus);

                    ctx->state = MSC_STATE_READ_CBW;
                    break;
                }

                // Determine next state based on data direction
                if(ctx->cbw.dDataLength > 0) {
                    if(ctx->cbw.bmFlags & 0x80) {
                        // Data IN (device to host)
                        FURI_LOG_D(TAG, "Transitioning to DATA_IN state, fall through");
                        ctx->state = MSC_STATE_DATA_IN;
                        ctx->tx_offset = 0;
                        // Fall through to DATA_IN case immediately
                        goto process_data_in;
                    } else {
                        // Data OUT (host to device)
                        ctx->state = MSC_STATE_DATA_OUT;
                        ctx->rx_len = 0;
                        // Break and wait for data from host
                        break;
                    }
                } else {
                    // No data phase - send CSW immediately
                    ctx->csw.bStatus = USB_MSC_CSW_STATUS_PASSED;
                    ctx->csw.dSignature = USB_MSC_CSW_SIGNATURE;
                    ctx->csw.dTag = ctx->cbw.dTag;
                    ctx->csw.dDataResidue = 0;

                    usbd_ep_write(dev, USB_MSC_EP_IN, &ctx->csw, sizeof(UsbMscCsw));
                    FURI_LOG_D(TAG, "CSW sent (no data): status=%u", ctx->csw.bStatus);

                    ctx->state = MSC_STATE_READ_CBW;
                    break;
                }
            }

            case MSC_STATE_DATA_IN:
            process_data_in: {
                // Check if SCSI layer has more data to send
                if(!usb_scsi_has_tx_data(ctx->scsi)) {
                    // Data transfer complete
                    FURI_LOG_D(TAG, "Data IN complete, total sent: %lu bytes", ctx->tx_offset);
                    ctx->csw.bStatus = USB_MSC_CSW_STATUS_PASSED;

                    // Prepare and send CSW
                    ctx->csw.dSignature = USB_MSC_CSW_SIGNATURE;
                    ctx->csw.dTag = ctx->cbw.dTag;

                    // Calculate residue: expected - actual
                    uint32_t residue = (ctx->cbw.dDataLength > ctx->tx_offset) ?
                                           (ctx->cbw.dDataLength - ctx->tx_offset) :
                                           0;
                    ctx->csw.dDataResidue = residue;

                    if(residue > 0) {
                        FURI_LOG_W(
                            TAG,
                            "Data residue: expected=%lu, sent=%lu, residue=%lu",
                            ctx->cbw.dDataLength,
                            ctx->tx_offset,
                            residue);
                    }

                    usbd_ep_write(dev, USB_MSC_EP_IN, &ctx->csw, sizeof(UsbMscCsw));
                    FURI_LOG_D(
                        TAG,
                        "CSW sent: status=%u, tag=%lu, residue=%lu",
                        ctx->csw.bStatus,
                        ctx->csw.dTag,
                        ctx->csw.dDataResidue);

                    ctx->state = MSC_STATE_READ_CBW;
                    ctx->tx_offset = 0; // Reset for next command
                    ctx->tx_len = 0;
                    break;
                }

                // If no data buffered, get new data from SCSI layer
                if(ctx->tx_len == 0) {
                    ctx->tx_len =
                        usb_scsi_transmit_data(ctx->scsi, ctx->tx_buffer, USB_MSC_EP_SIZE);

                    if(ctx->tx_len == 0) {
                        // Unexpected: SCSI said it has data but returned 0 bytes
                        FURI_LOG_E(TAG, "SCSI TX failed despite has_tx_data=true");
                        ctx->csw.bStatus = USB_MSC_CSW_STATUS_FAILED;

                        // Send error CSW
                        ctx->csw.dSignature = USB_MSC_CSW_SIGNATURE;
                        ctx->csw.dTag = ctx->cbw.dTag;

                        // Calculate residue: expected - actual
                        uint32_t residue = (ctx->cbw.dDataLength > ctx->tx_offset) ?
                                               (ctx->cbw.dDataLength - ctx->tx_offset) :
                                               0;
                        ctx->csw.dDataResidue = residue;

                        usbd_ep_write(dev, USB_MSC_EP_IN, &ctx->csw, sizeof(UsbMscCsw));
                        FURI_LOG_D(
                            TAG,
                            "CSW sent (error): status=%u, tag=%lu, residue=%lu",
                            ctx->csw.bStatus,
                            ctx->csw.dTag,
                            ctx->csw.dDataResidue);

                        ctx->state = MSC_STATE_READ_CBW;
                        ctx->tx_offset = 0;
                        ctx->tx_len = 0;
                        break;
                    }
                }

                // Try to send buffered data
                if(ctx->tx_len > 0) {
                    // FURI_LOG_D(TAG, "Sending %lu bytes (offset=%lu)", ctx->tx_len, ctx->tx_offset);
                    int32_t result =
                        usbd_ep_write(dev, USB_MSC_EP_IN, ctx->tx_buffer, ctx->tx_len);
                    if(result < 0) {
                        FURI_LOG_D(TAG, "usbd_ep_write busy, will retry");
                        // Endpoint busy - keep data in buffer and retry on next event
                        break;
                    }
                    // Success - mark as sent and clear buffer
                    ctx->tx_offset += ctx->tx_len;
                    ctx->tx_len = 0; // Clear buffer to fetch new data next time
                    // Stay in DATA_IN state, wait for TX complete event
                }
                break;
            }

            case MSC_STATE_DATA_OUT: {
                // Receive data from host
                int32_t len = usbd_ep_read(dev, USB_MSC_EP_OUT, ctx->rx_buffer, USB_MSC_EP_SIZE);

                if(len > 0) {
                    bool rx_ok = usb_scsi_receive_data(ctx->scsi, ctx->rx_buffer, len);
                    ctx->rx_len += len;

                    if(!rx_ok || ctx->rx_len >= ctx->cbw.dDataLength) {
                        // Data transfer complete or failed
                        ctx->csw.bStatus = rx_ok ? USB_MSC_CSW_STATUS_PASSED :
                                                   USB_MSC_CSW_STATUS_FAILED;

                        // Prepare and send CSW
                        ctx->csw.dSignature = USB_MSC_CSW_SIGNATURE;
                        ctx->csw.dTag = ctx->cbw.dTag;
                        ctx->csw.dDataResidue = 0;

                        usbd_ep_write(dev, USB_MSC_EP_IN, &ctx->csw, sizeof(UsbMscCsw));
                        FURI_LOG_D(TAG, "CSW sent: status=%u", ctx->csw.bStatus);

                        ctx->state = MSC_STATE_READ_CBW;
                    }
                }
                break;
            }

            case MSC_STATE_BUILD_CSW:
            case MSC_STATE_WRITE_CSW: {
                // Prepare and send CSW (fallback case)
                ctx->csw.dSignature = USB_MSC_CSW_SIGNATURE;
                ctx->csw.dTag = ctx->cbw.dTag;
                ctx->csw.dDataResidue = 0;
                // bStatus already set

                usbd_ep_write(dev, USB_MSC_EP_IN, &ctx->csw, sizeof(UsbMscCsw));
                FURI_LOG_D(TAG, "CSW sent: status=%u", ctx->csw.bStatus);

                ctx->state = MSC_STATE_READ_CBW;
                break;
            }

            case MSC_STATE_IDLE:
            default:
                ctx->state = MSC_STATE_READ_CBW;
                break;
            }
        }
    }

    ctx->thread_id = NULL;
    FURI_LOG_I(TAG, "Worker thread exited");
    return 0;
}

// USB interface initialization callbacks
static void msc_init(usbd_device* dev, FuriHalUsbInterface* intf, void* ctx) {
    FURI_LOG_I(TAG, "msc_init called: dev=%p, intf=%p, ctx=%p", dev, intf, ctx);
    UNUSED(intf);
    UNUSED(ctx);

    UsbMscContext* msc_ctx = g_msc_ctx;
    if(msc_ctx == NULL) {
        FURI_LOG_E(TAG, "MSC context not set in msc_init");
        return;
    }

    FURI_LOG_D(TAG, "Storing USB device: %p", dev);
    // Store USB device
    msc_ctx->usb_dev = dev;

    FURI_LOG_D(TAG, "Registering control callback");
    // Register control callback
    usbd_reg_control(dev, usb_msc_control);

    FURI_LOG_D(TAG, "Registering config callback");
    // Register config callback
    usbd_reg_config(dev, usb_msc_config);

    FURI_LOG_D(TAG, "Connecting USB");
    // Connect USB (descriptors handled by FuriHalUsbInterface)
    usbd_connect(dev, true);

    FURI_LOG_I(TAG, "USB MSC interface initialized successfully");
}

static void msc_deinit(usbd_device* dev) {
    UsbMscContext* msc_ctx = g_msc_ctx;
    if(msc_ctx == NULL) {
        return;
    }

    // Disconnect USB
    usbd_connect(dev, false);

    // Deconfigure endpoints
    usbd_reg_endpoint(dev, USB_MSC_EP_IN, NULL);
    usbd_reg_endpoint(dev, USB_MSC_EP_OUT, NULL);
    usbd_ep_deconfig(dev, USB_MSC_EP_IN);
    usbd_ep_deconfig(dev, USB_MSC_EP_OUT);

    msc_ctx->usb_dev = NULL;

    FURI_LOG_I(TAG, "USB MSC interface deinitialized");
}

static void msc_on_wakeup(usbd_device* dev) {
    UNUSED(dev);
}

static void msc_on_suspend(usbd_device* dev) {
    UNUSED(dev);
}

// USB interface structure
static FuriHalUsbInterface usb_msc_interface = {
    .init = msc_init,
    .deinit = msc_deinit,
    .wakeup = msc_on_wakeup,
    .suspend = msc_on_suspend,
    .dev_descr = (struct usb_device_descriptor*)&msc_device_desc,
    .cfg_descr = (struct usb_config_descriptor*)&msc_cfg_desc,
    .str_manuf_descr = (struct usb_string_descriptor*)&msc_string_manuf,
    .str_prod_descr = (struct usb_string_descriptor*)&msc_string_product,
    .str_serial_descr = (struct usb_string_descriptor*)&msc_string_serial,
};

bool usb_msc_start(UsbMscContext* ctx) {
    if(ctx == NULL || ctx->scsi == NULL) {
        FURI_LOG_E(TAG, "Invalid context: ctx=%p, scsi=%p", ctx, ctx ? ctx->scsi : NULL);
        return false;
    }

    if(ctx->active) {
        FURI_LOG_W(TAG, "Already active");
        return true;
    }

    FURI_LOG_I(TAG, "Starting USB MSC...");

    // Set global context for callbacks
    g_msc_ctx = ctx;

    // Create worker thread
    ctx->thread = furi_thread_alloc_ex("UsbMscWorker", 2048, mass_thread_worker, ctx);
    if(ctx->thread == NULL) {
        FURI_LOG_E(TAG, "Failed to allocate worker thread");
        g_msc_ctx = NULL;
        return false;
    }

    // Save current USB mode so we can restore it later
    FURI_LOG_D(TAG, "Saving current USB mode...");
    ctx->prev_usb_mode = (FuriHalUsbInterface*)furi_hal_usb_get_config();

    // Set USB configuration to MSC interface (it will handle locking internally)
    FURI_LOG_D(TAG, "Setting USB MSC config...");
    bool result = furi_hal_usb_set_config(&usb_msc_interface, NULL);
    if(!result) {
        FURI_LOG_E(TAG, "furi_hal_usb_set_config returned false - interface rejected");

        // Cleanup thread
        furi_thread_free(ctx->thread);
        ctx->thread = NULL;

        // Restore previous USB mode
        if(ctx->prev_usb_mode != NULL) {
            FURI_LOG_D(TAG, "Restoring previous mode: %p", ctx->prev_usb_mode);
            furi_hal_usb_set_config(ctx->prev_usb_mode, NULL);
        }

        g_msc_ctx = NULL;
        ctx->prev_usb_mode = NULL;
        return false;
    }

    // Start worker thread
    furi_thread_start(ctx->thread);

    FURI_LOG_I(TAG, "USB MSC started successfully with worker thread");
    ctx->active = true;

    return true;
}

void usb_msc_stop(UsbMscContext* ctx) {
    if(ctx == NULL || !ctx->active) {
        return;
    }

    FURI_LOG_I(TAG, "Stopping USB MSC...");

    // Signal worker thread to exit
    if(ctx->thread_id != NULL) {
        furi_thread_flags_set(ctx->thread_id, EventExit);

        // Wait for thread to finish
        if(ctx->thread != NULL) {
            furi_thread_join(ctx->thread);
            furi_thread_free(ctx->thread);
            ctx->thread = NULL;
        }
    }

    // Restore previous USB mode
    if(ctx->prev_usb_mode != NULL) {
        FURI_LOG_D(TAG, "Restoring previous USB mode: %p", ctx->prev_usb_mode);
        furi_hal_usb_set_config(ctx->prev_usb_mode, NULL);
        ctx->prev_usb_mode = NULL;
    } else {
        // No previous mode saved, just disable USB MSC
        FURI_LOG_D(TAG, "No previous mode saved, disabling USB");
        furi_hal_usb_set_config(NULL, NULL);
    }

    g_msc_ctx = NULL;
    ctx->active = false;
    ctx->state = MSC_STATE_IDLE;

    FURI_LOG_I(TAG, "USB MSC stopped");
}

bool usb_msc_is_active(UsbMscContext* ctx) {
    return ctx != NULL && ctx->active;
}
