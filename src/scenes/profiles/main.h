#pragma once

#include <furi.h>
#include <gui/view.h>
#include <gui/modules/submenu.h>

typedef enum {
    ProfilesItemLoad,
    ProfilesItemSaveAs,
    ProfilesItemDelete,
} ProfilesItem;

typedef struct {
    Submenu* submenu;
    FuriString* header;
} AppProfiles;

AppProfiles* Profiles_alloc();
View* Profiles_get_view(void* p);
void Profiles_free(void* p);
void Profiles_on_enter(void* p);
bool Profiles_on_event(void* p, SceneManagerEvent e);
void Profiles_on_exit(void* p);
