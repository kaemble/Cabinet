// gcc -O2 -Wall dump_core_options.c -o dump_core_options -ldl

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>

#define RETRO_ENVIRONMENT_GET_OVERSCAN                2
#define RETRO_ENVIRONMENT_GET_CAN_DUPE                3
#define RETRO_ENVIRONMENT_SET_VARIABLES              16
#define RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE        17
#define RETRO_ENVIRONMENT_GET_LIBRETRO_PATH          19
#define RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY       31
#define RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY         32
#define RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION   52
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS           53
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL      54
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY   55
#define RETRO_ENVIRONMENT_GET_PREFERRED_HW           56
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2        67
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL   68

#define RETRO_NUM_CORE_OPTION_VALUES_MAX 128

struct retro_variable {
    const char *key;
    const char *value;
};

struct retro_core_option_value {
    const char *value;
    const char *label;
};

struct retro_core_option_definition {
    const char *key;
    const char *desc;
    const char *info;
    struct retro_core_option_value values[RETRO_NUM_CORE_OPTION_VALUES_MAX];
    const char *default_value;
};

struct retro_core_options_intl {
    struct retro_core_option_definition *us;
    struct retro_core_option_definition *local;
};

struct retro_core_option_v2_category {
    const char *key;
    const char *desc;
    const char *info;
};

struct retro_core_option_v2_definition {
    const char *key;
    const char *desc;
    const char *desc_categorized;
    const char *info;
    const char *info_categorized;
    const char *category_key;
    struct retro_core_option_value values[RETRO_NUM_CORE_OPTION_VALUES_MAX];
    const char *default_value;
};

struct retro_core_options_v2 {
    struct retro_core_option_v2_category *categories;
    struct retro_core_option_v2_definition *definitions;
};

struct retro_core_options_v2_intl {
    struct retro_core_options_v2 *us;
    struct retro_core_options_v2 *local;
};

typedef bool (*retro_environment_t)(unsigned cmd, void *data);

// print options
static bool options_found = false;

static void print_legacy_variables(const struct retro_variable *vars) {
    printf("=== Legacy Core Options (SET_VARIABLES) ===\n\n");
    for (size_t i = 0; vars[i].key != NULL; i++) {
        options_found = true;
        printf("Key: %s\n", vars[i].key);
        if (vars[i].value) {
            /* Format is usually "Description; val1|val2|val3" */
            const char *semi = strchr(vars[i].value, ';');
            if (semi) {
                printf("  Description: %.*s\n", (int)(semi - vars[i].value), vars[i].value);
                const char *choices = semi + 1;
                while (*choices == ' ') choices++;
                printf("  Accepted Values: %s\n", choices);
            } else {
                printf("  Raw Value: %s\n", vars[i].value);
            }
        }
        printf("\n");
    }
}

static void print_v1_options(const struct retro_core_option_definition *defs) {
    printf("=== Core Options V1 (SET_CORE_OPTIONS) ===\n\n");
    for (size_t i = 0; defs[i].key != NULL; i++) {
        options_found = true;
        printf("Key: %s\n", defs[i].key);
        if (defs[i].desc)          printf("  Label: %s\n", defs[i].desc);
        if (defs[i].info)          printf("  Info:  %s\n", defs[i].info);
        if (defs[i].default_value) printf("  Default: %s\n", defs[i].default_value);
        
        printf("  Accepted Values:\n");
        for (size_t j = 0; j < RETRO_NUM_CORE_OPTION_VALUES_MAX && defs[i].values[j].value != NULL; j++) {
            if (defs[i].values[j].label) {
                printf("    - %s (%s)\n", defs[i].values[j].value, defs[i].values[j].label);
            } else {
                printf("    - %s\n", defs[i].values[j].value);
            }
        }
        printf("\n");
    }
}

static void print_v2_options(const struct retro_core_options_v2 *v2) {
    printf("=== Core Options V2 (SET_CORE_OPTIONS_V2) ===\n\n");
    if (!v2 || !v2->definitions) return;

    for (size_t i = 0; v2->definitions[i].key != NULL; i++) {
        const struct retro_core_option_v2_definition *def = &v2->definitions[i];
        options_found = true;
        printf("Key: %s\n", def->key);
        if (def->category_key)  printf("  Category: %s\n", def->category_key);
        if (def->desc)          printf("  Label:    %s\n", def->desc);
        if (def->info)          printf("  Info:     %s\n", def->info);
        if (def->default_value) printf("  Default:  %s\n", def->default_value);

        printf("  Accepted Values:\n");
        for (size_t j = 0; j < RETRO_NUM_CORE_OPTION_VALUES_MAX && def->values[j].value != NULL; j++) {
            if (def->values[j].label) {
                printf("    - %s (%s)\n", def->values[j].value, def->values[j].label);
            } else {
                printf("    - %s\n", def->values[j].value);
            }
        }
        printf("\n");
    }
}

// environment callback
static bool core_environment_callback(unsigned cmd, void *data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
            if (data) *(unsigned *)data = 2; /* Tell core we support V2 */
            return true;

        case RETRO_ENVIRONMENT_SET_VARIABLES:
            if (data) print_legacy_variables((const struct retro_variable *)data);
            return true;

        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
            if (data) print_v1_options((const struct retro_core_option_definition *)data);
            return true;

        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
            if (data) {
                const struct retro_core_options_intl *intl = (const struct retro_core_options_intl *)data;
                if (intl->us) print_v1_options(intl->us);
            }
            return true;

        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
            if (data) print_v2_options((const struct retro_core_options_v2 *)data);
            return true;

        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
            if (data) {
                const struct retro_core_options_v2_intl *intl = (const struct retro_core_options_v2_intl *)data;
                if (intl->us) print_v2_options(intl->us);
            }
            return true;

        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
            if (data) *(const char **)data = "."; /* Provide dummy path so cores don't abort */
            return true;

        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            if (data) *(bool *)data = true;
            return true;

        default:
            return false;
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path_to_core.so>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *core_path = argv[1];
    void *handle = dlopen(core_path, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "Failed to load core '%s': %s\n", core_path, dlerror());
        return EXIT_FAILURE;
    }

    /* Load required Libretro symbols */
    void (*retro_set_environment)(retro_environment_t) = dlsym(handle, "retro_set_environment");
    void (*retro_init)(void)                           = dlsym(handle, "retro_init");
    void (*retro_deinit)(void)                         = dlsym(handle, "retro_deinit");

    if (!retro_set_environment || !retro_init) {
        fprintf(stderr, "Invalid Libretro core: missing required symbols.\n");
        dlclose(handle);
        return EXIT_FAILURE;
    }

    /* Register environment callback and initialize core */
    retro_set_environment(core_environment_callback);
    retro_init();

    if (!options_found) {
        printf("No core options were registered during retro_init().\n");
        printf("(Note: Some cores only register options inside retro_load_game.)\n");
    }

    if (retro_deinit) {
        retro_deinit();
    }

    dlclose(handle);
    return EXIT_SUCCESS;
}
