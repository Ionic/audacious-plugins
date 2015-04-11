/*
 * Copyright (c) 2010 William Pitcock <nenolod@dereferenced.org>.
 * Copyright (c) 2010-2011 John Lindgren <john.lindgren@tds.net>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <string.h>

#undef EXPORT
#include <mpg123.h>

// mpg123.h redefines EXPORT
#undef EXPORT
#include "config.h"

#ifdef DEBUG_MPG123_IO
# define MPG123_IODBG(...)  AUDDBG(__VA_ARGS__)
#else
# define MPG123_IODBG(...)  do { } while (0)
#endif

#define WANT_VFS_STDIO_COMPAT
#include <libaudcore/audstrings.h>
#include <libaudcore/runtime.h>
#include <libaudcore/i18n.h>
#include <libaudcore/plugin.h>
#include <libaudcore/preferences.h>
#include <audacious/audtag.h>

class MPG123Plugin : public InputPlugin
{
public:
    static const char * const exts[];
    static const char * const defaults[];
    static const PreferencesWidget widgets[];
    static const PluginPreferences prefs;

    static constexpr PluginInfo info = {
        N_("MPG123 Plugin"),
        PACKAGE,
        nullptr,
        & prefs
    };

    static constexpr auto iinfo = InputInfo (FlagWritesTag)
        .with_exts (exts);

    constexpr MPG123Plugin() : InputPlugin (info, iinfo) {}

    bool init ();
    void cleanup ();

    bool is_our_file (const char * filename, VFSFile & file);
    Tuple read_tuple (const char * filename, VFSFile & file);
    Index<char> read_image (const char * filename, VFSFile & file);
    bool write_tuple (const char * filename, VFSFile & file, const Tuple & tuple);
    bool play (const char * filename, VFSFile & file);
};

EXPORT MPG123Plugin aud_plugin_instance;

const char * const MPG123Plugin::defaults[] = {
    "full_scan", "FALSE",
    nullptr
};

const PreferencesWidget MPG123Plugin::widgets[] = {
    WidgetLabel (N_("<b>Advanced</b>")),
    WidgetCheck (N_("Use accurate length calculation (slow)"),
        WidgetBool ("mpg123", "full_scan"))
};

const PluginPreferences MPG123Plugin::prefs = {{widgets}};

#define DECODE_OPTIONS (MPG123_QUIET | MPG123_GAPLESS | MPG123_SEEKBUFFER | MPG123_FUZZY)

static ssize_t replace_read (void * file, void * buffer, size_t length)
{
    return ((VFSFile *) file)->fread (buffer, 1, length);
}

static off_t replace_lseek (void * file, off_t to, int whence)
{
    if (((VFSFile *) file)->fseek (to, to_vfs_seek_type (whence)) < 0)
        return -1;

    return ((VFSFile *) file)->ftell ();
}

static off_t replace_lseek_dummy (void * file, off_t to, int whence)
{
    return -1;
}

bool MPG123Plugin::init ()
{
    aud_config_set_defaults ("mpg123", defaults);

    AUDDBG("initializing mpg123 library\n");
    mpg123_init();

    return true;
}

void MPG123Plugin::cleanup ()
{
    AUDDBG("deinitializing mpg123 library\n");
    mpg123_exit();
}

static void set_format (mpg123_handle * dec)
{
    mpg123_format_none (dec);

    for (int rate : {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000})
        mpg123_format (dec, rate, MPG123_MONO | MPG123_STEREO, MPG123_ENC_FLOAT_32);
}

static void make_format_string (const struct mpg123_frameinfo * info, char *
 buf, int bsize)
{
    static const char * vers[] = {"1", "2", "2.5"};
    snprintf (buf, bsize, "MPEG-%s layer %d", vers[info->version], info->layer);
}

bool MPG123Plugin::is_our_file (const char * fname, VFSFile & file)
{
    /* MPG123 likes to grab WMA streams, so blacklist anything that starts with
     * mms://.  If there are mms:// streams out there carrying MP3, they will
     * just have to play in ffaudio.  --jlindgren */
    if (! strncmp (fname, "mms://", 6))
        return false;

    bool is_streaming = (file.fsize () < 0);

    /* Some MP3s begin with enormous ID3 tags, which fill up the whole probe
     * buffer and thus hide any MP3 content.  As a workaround, assume that an
     * ID3 tag means an MP3 file.  --jlindgren */
    if (! is_streaming)
    {
        char id3buf[3];
        if (file.fread (id3buf, 1, 3) != 3)
            return false;

        if (! memcmp (id3buf, "ID3", 3))
            return true;

        if (file.fseek (0, VFS_SEEK_SET) < 0)
            return false;
    }

    mpg123_handle * dec = mpg123_new (nullptr, nullptr);
    mpg123_param (dec, MPG123_ADD_FLAGS, DECODE_OPTIONS, 0);

    if (is_streaming)
        mpg123_replace_reader_handle (dec, replace_read, replace_lseek_dummy, nullptr);
    else
        mpg123_replace_reader_handle (dec, replace_read, replace_lseek, nullptr);

    set_format (dec);

    int res;
    if ((res = mpg123_open_handle (dec, & file)) < 0)
    {
ERR:
        AUDDBG ("Probe error: %s\n", mpg123_plain_strerror (res));
        mpg123_delete (dec);
        return false;
    }

    if (! is_streaming && aud_get_bool ("mpg123", "full_scan") && mpg123_scan (dec) < 0)
        goto ERR;

RETRY:;
    long rate;
    int chan, enc;
    if ((res = mpg123_getformat (dec, & rate, & chan, & enc)) < 0)
        goto ERR;

    struct mpg123_frameinfo info;
    if ((res = mpg123_info (dec, & info)) < 0)
        goto ERR;

    float out[8192];
    size_t done;
    while ((res = mpg123_read (dec, (unsigned char *) out, sizeof out, & done)) < 0)
    {
        if (res == MPG123_NEW_FORMAT)
            goto RETRY;
        goto ERR;
    }

    char str[32];
    make_format_string (& info, str, sizeof str);
    AUDDBG ("Accepted as %s: %s.\n", str, fname);

    mpg123_delete (dec);
    return true;
}

Tuple MPG123Plugin::read_tuple (const char * filename, VFSFile & file)
{
    bool stream = (file.fsize () < 0);
    mpg123_handle * decoder = mpg123_new (nullptr, nullptr);
    int result;
    long rate;
    int channels, encoding;
    struct mpg123_frameinfo info;
    char scratch[32];

    mpg123_param (decoder, MPG123_ADD_FLAGS, DECODE_OPTIONS, 0);

    if (stream)
        mpg123_replace_reader_handle (decoder, replace_read, replace_lseek_dummy, nullptr);
    else
        mpg123_replace_reader_handle (decoder, replace_read, replace_lseek, nullptr);

    if ((result = mpg123_open_handle (decoder, & file)) < 0
     || (! stream && aud_get_bool ("mpg123", "full_scan") && (result = mpg123_scan (decoder)) < 0)
     || (result = mpg123_getformat (decoder, & rate, & channels, & encoding)) < 0
     || (result = mpg123_info (decoder, & info)) < 0)
    {
        AUDERR ("mpg123 probe error for %s: %s\n", filename, mpg123_plain_strerror (result));
        mpg123_delete (decoder);
        return Tuple ();
    }

    Tuple tuple;
    tuple.set_filename (filename);
    make_format_string (& info, scratch, sizeof scratch);
    tuple.set_str (Tuple::Codec, scratch);
    snprintf (scratch, sizeof scratch, "%s, %d Hz", (channels == 2)
     ? _("Stereo") : (channels > 2) ? _("Surround") : _("Mono"), (int) rate);
    tuple.set_str (Tuple::Quality, scratch);
    tuple.set_int (Tuple::Bitrate, info.bitrate);

    if (! stream)
    {
        int64_t size = file.fsize ();
        int64_t samples = mpg123_length (decoder);
        int length = (samples > 0 && rate > 0) ? samples * 1000 / rate : 0;

        if (length > 0)
            tuple.set_int (Tuple::Length, length);
        if (size > 0 && length > 0)
            tuple.set_int (Tuple::Bitrate, 8 * size / length);
    }

    mpg123_delete (decoder);

    if (! stream && ! file.fseek (0, VFS_SEEK_SET))
        audtag::tuple_read (tuple, file);

    if (stream)
        tuple.fetch_stream_info (file);

    return tuple;
}

typedef struct {
    mpg123_handle *decoder;
    long rate;
    int channels;
    int encoding;
    bool stream;
    Tuple tu;
} MPG123PlaybackContext;

static void print_mpg123_error (const char * filename, mpg123_handle * decoder)
{
    AUDERR ("mpg123 error in %s: %s\n", filename, mpg123_strerror (decoder));
}

bool MPG123Plugin::play (const char * filename, VFSFile & file)
{
    bool error = false;
    MPG123PlaybackContext ctx;
    int ret;
    int bitrate = 0, bitrate_sum = 0, bitrate_count = 0;
    struct mpg123_frameinfo fi;
    int error_count = 0;

    memset(&ctx, 0, sizeof(MPG123PlaybackContext));
    memset(&fi, 0, sizeof(struct mpg123_frameinfo));

    AUDDBG("playback worker started for %s\n", filename);

    AUDDBG ("Checking for streaming ...\n");
    ctx.stream = (file.fsize () < 0);
    ctx.tu = ctx.stream ? get_playback_tuple () : Tuple ();

    ctx.decoder = mpg123_new (nullptr, nullptr);
    mpg123_param (ctx.decoder, MPG123_ADD_FLAGS, DECODE_OPTIONS, 0);

    if (ctx.stream)
        mpg123_replace_reader_handle (ctx.decoder, replace_read, replace_lseek_dummy, nullptr);
    else
        mpg123_replace_reader_handle (ctx.decoder, replace_read, replace_lseek, nullptr);

    set_format (ctx.decoder);

    float outbuf[8192];
    size_t outbuf_size = 0;

    if (mpg123_open_handle (ctx.decoder, & file) < 0)
    {
OPEN_ERROR:
        print_mpg123_error (filename, ctx.decoder);
        error = true;
        goto cleanup;
    }

    if (! ctx.stream && aud_get_bool ("mpg123", "full_scan") && mpg123_scan (ctx.decoder) < 0)
        goto OPEN_ERROR;

GET_FORMAT:
    if (mpg123_getformat (ctx.decoder, & ctx.rate, & ctx.channels,
     & ctx.encoding) < 0)
        goto OPEN_ERROR;

    while ((ret = mpg123_read (ctx.decoder, (unsigned char *) outbuf,
     sizeof outbuf, & outbuf_size)) < 0)
    {
        if (ret == MPG123_NEW_FORMAT)
            goto GET_FORMAT;
        goto OPEN_ERROR;
    }

    if (mpg123_info (ctx.decoder, & fi) < 0)
        goto OPEN_ERROR;

    bitrate = fi.bitrate * 1000;
    set_stream_bitrate (bitrate);

    if (ctx.tu && ctx.tu.fetch_stream_info (file))
        set_playback_tuple (ctx.tu.ref ());

    open_audio (FMT_FLOAT, ctx.rate, ctx.channels);

    while (! check_stop ())
    {
        int seek = check_seek ();

        if (seek >= 0)
        {
            if (mpg123_seek (ctx.decoder, (int64_t) seek * ctx.rate / 1000, SEEK_SET) < 0)
                print_mpg123_error (filename, ctx.decoder);

            outbuf_size = 0;
        }

        mpg123_info(ctx.decoder, &fi);
        bitrate_sum += fi.bitrate;
        bitrate_count ++;

        if (bitrate_sum / bitrate_count != bitrate && bitrate_count >= 16)
        {
            set_stream_bitrate (bitrate_sum / bitrate_count * 1000);
            bitrate = bitrate_sum / bitrate_count;
            bitrate_sum = 0;
            bitrate_count = 0;
        }

        if (ctx.tu && ctx.tu.fetch_stream_info (file))
            set_playback_tuple (ctx.tu.ref ());

        if (! outbuf_size && (ret = mpg123_read (ctx.decoder,
         (unsigned char *) outbuf, sizeof outbuf, & outbuf_size)) < 0)
        {
            if (ret == MPG123_DONE || ret == MPG123_ERR_READER)
                break;

            print_mpg123_error (filename, ctx.decoder);

            if (++ error_count >= 10)
            {
                error = true;
                break;
            }
        }
        else
        {
            error_count = 0;

            write_audio (outbuf, outbuf_size);
            outbuf_size = 0;
        }
    }

cleanup:
    mpg123_delete(ctx.decoder);
    return ! error;
}

bool MPG123Plugin::write_tuple (const char * filename, VFSFile & file, const Tuple & tuple)
{
    if (file.fsize () < 0)  // stream?
        return false;

    return audtag::tuple_write (tuple, file, audtag::TagType::ID3v2);
}

Index<char> MPG123Plugin::read_image (const char * filename, VFSFile & file)
{
    if (file.fsize () < 0)  // stream?
        return Index<char> ();

    return audtag::image_read (file);
}

const char * const MPG123Plugin::exts[] = { "mp3", "mp2", "mp1", "bmu", nullptr };
