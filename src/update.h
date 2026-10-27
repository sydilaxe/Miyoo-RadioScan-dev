/* Self-update: ask GitHub for the latest release, and if it's newer than this build, download its
 * RadioScan.zip and swap the new files into the app folder. stations.txt and settings.cfg stay as they are. */
#ifndef UPDATE_H
#define UPDATE_H

#include <stddef.h>

#define RS_VERSION "0.8.1" /* keep in step with the release tag (v0.8.1); CI refuses to publish a mismatch */
#define RS_REPO "sydilaxe/RadioScan"

/* exit status that tells launch.sh to start the app again (the new version) */
#define RELAUNCH_STATUS 75

enum {
    UP_IDLE,       /* not checked yet */
    UP_CHECKING,
    UP_CURRENT,    /* this is the latest release */
    UP_AVAILABLE,  /* a newer release is out: latest, url */
    UP_CHECK_FAILED,
    UP_INSTALLING, /* progress 0..100 */
    UP_INSTALLED,  /* the new files are in place: relaunch */
    UP_FAILED      /* install failed: err (the old version is untouched) */
};

typedef struct {
    volatile int state;
    volatile int progress;
    char latest[32]; /* "0.8.1" */
    char url[1024];  /* the release's RadioScan.zip */
    char err[160];
} UpdateStatus;

const UpdateStatus *update_status(void);
void update_check_start(void);                 /* in the background; state goes CHECKING -> CURRENT / AVAILABLE / CHECK_FAILED */
void update_install_start(const char *app_dir); /* from AVAILABLE; state goes INSTALLING -> INSTALLED / FAILED */

/* blocking versions, for the command line and tests; 0 on success */
int update_check(void);
int update_install(const char *app_dir);

/* true if dotted version a ("v0.8.1" or "0.8.1") is newer than b */
int version_newer(const char *a, const char *b);
/* unpack the RadioScan/ folder of a zip into dir, keeping the user's files; 0 on success */
int update_unzip(const unsigned char *zip, long len, const char *dir, char *err, size_t errlen);

#endif
