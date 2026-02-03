#pragma once

typedef struct {
    const char *version;
    const char *buildtime;
    const char *commit;
} verinfo_t;

static const verinfo_t verinfo = {
    .version = BUILD_VERSION,
    .buildtime = BUILD_TIME,
    .commit = COMMIT,
};
