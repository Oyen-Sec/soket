#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/inotify.h>
#include <limits.h>
#include <errno.h>
#include <pwd.h>
#include <dirent.h>
#include <sys/stat.h>




static const char *TG_TOKEN = "8602911604:AAGZs2G4n1DNFc9zzAcmsZyYdEWP1ARXh80";
static const char *TG_CHAT_ID = "5439698489";


static const char *WATCH_TARGETS[] = {
    "/tmp/www/html", 
    "/var/www/html", "/var/www", "/public_html", "/www", 
    "/usr/share/nginx/html", "/htdocs", "/opt/lampp/htdocs"
};

#define MAX_WATCHES 4096
#define EVENT_MASK (IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO)

typedef struct {
    int wd;
    char path[PATH_MAX];
} ph_watch_map_t;

static ph_watch_map_t watch_map[MAX_WATCHES];
static int watch_count = 0;

static const char* get_path_from_wd(int wd) {
    for (int i = 0; i < watch_count; i++) {
        if (watch_map[i].wd == wd) return watch_map[i].path;
    }
    return NULL;
}

static void ph_sentinel_push_telegram(const char *event_name, const char *base_path, const char *file_name) {
    char cmd[4096];
    char payload[2048];
    char sys_hostname[256] = "UNKNOWN";
    char sys_username[256] = "UNKNOWN";

    gethostname(sys_hostname, sizeof(sys_hostname));
    struct passwd *pw = getpwuid(geteuid());
    if (pw) strncpy(sys_username, pw->pw_name, sizeof(sys_username) - 1);

    
    time_t now = time(NULL); 
    struct tm *t = localtime(&now); 
    char time_str[64]; 
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t); 
  
    
    char public_ip[64] = "UNKNOWN_IP"; 
    FILE *fp = popen("curl -s ifconfig.me", "r"); 
    if (fp != NULL) { 
        fgets(public_ip, sizeof(public_ip)-1, fp); 
        pclose(fp); 
    } 

    
    
    snprintf(payload, sizeof(payload), 
        "text=[SENTINEL ALERT] DEEP FIM TRIGGERED%%0A" 
        "<b>TIME</b>      : <code>%s</code>%%0A" 
        "<b>IP</b>        : <code>%s</code>%%0A" 
        "<b>HOSTNAME</b>  : <code>%s</code>%%0A" 
        "<b>USER</b>      : <code>%s</code>%%0A" 
        "<b>EVENT</b>     : <code>%s</code>%%0A" 
        "<b>FULL_PATH</b> : <code>%s/%s</code>", 
        time_str, public_ip, sys_hostname, sys_username, event_name, base_path, file_name ? file_name : "");

    snprintf(cmd, sizeof(cmd), 
        "curl -s -X POST https://api.telegram.org/bot%s/sendMessage "
        "-d chat_id=%s -d parse_mode=HTML -d \"%s\" > /tmp/sentinel_telegram_debug.log 2>&1",
        TG_TOKEN, TG_CHAT_ID, payload);
    
    if (system(cmd) == -1) {}
}

static void watch_directory_recursively(int fd, const char *dir_path) {
    if (watch_count >= MAX_WATCHES) return;

    
    if (strstr(dir_path, "node_modules") || strstr(dir_path, ".git") || strstr(dir_path, "vendor")) return;

    int wd = inotify_add_watch(fd, dir_path, EVENT_MASK);
    if (wd < 0) return;

    watch_map[watch_count].wd = wd;
    strncpy(watch_map[watch_count].path, dir_path, PATH_MAX - 1);
    watch_count++;

    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char sub_path[PATH_MAX];
        snprintf(sub_path, sizeof(sub_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (lstat(sub_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            watch_directory_recursively(fd, sub_path);
        }
    }
    closedir(dir);
}

int main(void) {
    if (daemon(0, 0) < 0) return 1;

    int fd = inotify_init();
    if (fd < 0) return 1;

    for (size_t i = 0; i < sizeof(WATCH_TARGETS)/sizeof(WATCH_TARGETS[0]); i++) {
        struct stat st;
        if (stat(WATCH_TARGETS[i], &st) == 0 && S_ISDIR(st.st_mode)) {
            watch_directory_recursively(fd, WATCH_TARGETS[i]);
        }
    }

    char buffer[16384] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    while (1) {
        ssize_t len = read(fd, buffer, sizeof(buffer));
        if (len <= 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (char *ptr = buffer; ptr < buffer + len; ) {
            struct inotify_event *event = (struct inotify_event *)ptr;
            ptr += sizeof(struct inotify_event) + event->len;

            if (event->len == 0) continue;

            
            if (strstr(event->name, "systemd") || strstr(event->name, "sessionclean") || 
                strstr(event->name, "logrotate") || strstr(event->name, ".log") || 
                event->name[0] == '.' || event->name[0] == '#') { 
                continue; 
            }

            const char *base_path = get_path_from_wd(event->wd);
            if (!base_path) continue;

            const char *event_type = "UNKNOWN";
            if (event->mask & IN_CREATE) event_type = "CREATE";
            else if (event->mask & IN_MODIFY) event_type = "MODIFY";
            else if (event->mask & IN_DELETE) event_type = "DELETE";
            else if (event->mask & (IN_MOVED_FROM | IN_MOVED_TO)) event_type = "RENAME";

            ph_sentinel_push_telegram(event_type, base_path, event->name);

            
            if ((event->mask & IN_CREATE) && (event->mask & IN_ISDIR)) {
                char new_path[PATH_MAX];
                snprintf(new_path, sizeof(new_path), "%s/%s", base_path, event->name);
                watch_directory_recursively(fd, new_path);
            }
        }
    }

    close(fd);
    return 0;
}
