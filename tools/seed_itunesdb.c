#include <gpod/itdb.h>
#include <glib.h>

#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

static void fail_gerror(const char *operation, GError *error) {
    fprintf(stderr, "%s: %s\n", operation, error ? error->message : "unknown error");
    if (error) {
        g_error_free(error);
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s MOUNTPOINT MP3\n", argv[0]);
        return 2;
    }

    const char *mountpoint = argv[1];
    const char *source_mp3 = argv[2];
    gchar *itunes_dir = g_build_filename(mountpoint, "iPod_Control", "iTunes", NULL);
    gchar *music_dir = g_build_filename(mountpoint, "iPod_Control", "Music", NULL);
    if (g_mkdir_with_parents(itunes_dir, 0755) != 0 ||
        g_mkdir_with_parents(music_dir, 0755) != 0) {
        fprintf(stderr, "failed to create iPod_Control directories\n");
        g_free(music_dir);
        g_free(itunes_dir);
        return 1;
    }
    for (int i = 0; i < 20; ++i) {
        gchar *name = g_strdup_printf("F%02d", i);
        gchar *path = g_build_filename(music_dir, name, NULL);
        if (g_mkdir_with_parents(path, 0755) != 0) {
            fprintf(stderr, "failed to create %s\n", path);
            g_free(path);
            g_free(name);
            g_free(music_dir);
            g_free(itunes_dir);
            return 1;
        }
        g_free(path);
        g_free(name);
    }

    struct stat st;
    if (stat(source_mp3, &st) != 0) {
        perror(source_mp3);
        g_free(music_dir);
        g_free(itunes_dir);
        return 1;
    }

    Itdb_iTunesDB *db = itdb_new();
    itdb_set_mountpoint(db, mountpoint);
    itdb_device_set_sysinfo(db->device, "ModelNumStr", "MA004");

    Itdb_Playlist *master = itdb_playlist_new("iPod", FALSE);
    itdb_playlist_set_mpl(master);
    itdb_playlist_add(db, master, -1);

    Itdb_Track *track = itdb_track_new();
    track->title = g_strdup("Nano Test Loop");
    track->album = g_strdup("Firmware Lab");
    track->artist = g_strdup("OpenAI Codex");
    track->genre = g_strdup("Electronic");
    track->composer = g_strdup("OpenAI Codex");
    track->filetype = g_strdup("MPEG audio file");
    track->size = (guint32)st.st_size;
    track->tracklen = 6034;
    track->track_nr = 1;
    track->tracks = 1;
    track->bitrate = 128;
    track->samplerate = 44100;
    track->samplerate2 = 44100.0f;
    track->year = 2026;
    track->time_added = 1767225600;
    track->time_modified = 1767225600;
    track->mediatype = ITDB_MEDIATYPE_AUDIO;
    track->visible = 1;
    track->checked = 1;
    itdb_track_add(db, track, -1);
    itdb_playlist_add_track(master, track, -1);

    GError *error = NULL;
    if (!itdb_cp_track_to_ipod(track, source_mp3, &error)) {
        fail_gerror("copy track", error);
        itdb_free(db);
        g_free(music_dir);
        g_free(itunes_dir);
        return 1;
    }
    if (!itdb_write(db, &error)) {
        fail_gerror("write iTunesDB", error);
        itdb_free(db);
        g_free(music_dir);
        g_free(itunes_dir);
        return 1;
    }

    printf("wrote %s with one track at %s\n", itunes_dir, track->ipod_path);
    itdb_free(db);
    g_free(music_dir);
    g_free(itunes_dir);
    return 0;
}
