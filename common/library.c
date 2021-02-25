/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <string.h>

#include "../common/debug.h"
#include "library.h"

#define D3DADAPTER9 "d3dadapter9.so.1"

static void *open_d3dadapter(char *paths, char **res, char **err)
{
    char *next, *end, *p, *lasterr = NULL;
    void *handle = NULL;
    char path[4096];
    struct stat st;
    int len;

    end = paths + strlen(paths);
    for (p = paths; p < end; p = next + 1)
    {
        next = strchr(p, ':');
        if (!next)
            next = end;

        len = next - p;
        snprintf(path, sizeof(path), "%.*s", len, p);

        if (!stat(path, &st) && S_ISDIR(st.st_mode))
            strcat(path, "/" D3DADAPTER9);

        TRACE("Trying to load '%s'\n", path);
        handle = dlopen(path, RTLD_GLOBAL | RTLD_NOW);

        if (handle) {
            if (res)
              *res = strdup(path);

            break;
        }

        free(lasterr);
        lasterr = strdup(dlerror());

        TRACE("Failed to load '%s': %s\n", path, lasterr);
    }

    if (handle || !err)
    {
        free(lasterr);
        lasterr = NULL;
    }

    if (handle)
        TRACE("Loaded '%s'\n", path);

    if (err)
        *err = lasterr;

    return handle;
}

void *common_load_d3dadapter(char **path, char **err)
{
    static void *handle = NULL;
    char *env;

    env = getenv("D3D_MODULE_PATH");
    if (env)
    {
        handle = open_d3dadapter(env, path, err);

        if (!handle)
            ERR("Failed to load " D3DADAPTER9 " set by D3D_MODULE_PATH (%s)\n", env);

        return handle;
    }

#if defined(D3D9NINE_MODULEPATH)
    handle = open_d3dadapter(D3D9NINE_MODULEPATH, path, err);

    if (!handle)
        ERR("Failed to load " D3DADAPTER9 " set by builtin default '%s'\n",
            D3D9NINE_MODULEPATH);

    return handle;
#else
    handle = open_d3dadapter("/usr/lib/x86_64-linux-gnu/d3d:"        // 64bit debian/ubuntu
                             "/usr/lib/i386-linux-gnu/d3d:"          // 32bit debian/ubuntu
                             "/usr/lib64/d3d:"                       // 64bit gentoo/suse/fedora
                             "/usr/lib/d3d:"                         // 32bit suse/fedora, 64bit arch
                             "/usr/lib32/d3d:"                       // 32bit arch/gentoo
                             "/usr/lib/x86_64-linux-gnu/GL/lib/d3d:" // 64bit flatpak runtime
                             "/usr/lib/i386-linux-gnu/GL/lib/d3d"    // 32bit flatpak runtime
                             , path, err);

    if (!handle)
        ERR(D3DADAPTER9 " was not found on your system.\n"
            "Setting the envvar D3D_MODULE_PATH is required\n");

    return handle;
#endif
}
