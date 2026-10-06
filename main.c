/*
 * Static site generator for notfound404.dev.
 *
 * Builds every page with htmc and writes it to ./out. Run from the project root:
 *
 *     ./build.sh ./deps/htmc/htm.c main.c          development build (default)
 *     ./build.sh ./deps/htmc/htm.c main.c --prod   production build + out/nginx.conf
 *
 * (build.sh passes --dev / --prod to ./output.o and everything else to clang.)
 *
 * Inputs read at build time (relative to the project root):
 *   deps/js/tailwind.js   Tailwind v4 browser build, inlined on every page
 *   deps/js/highlight*    highlight.js and its theme, inlined on article pages
 *   js/NAME.js            component scripts, inlined inside an IIFE
 *   css/NAME.css          stylesheets, inlined in <style> (@BASE@ filled in)
 *   config/nginx.conf     template of out/nginx.conf, prod only (@SITE_NAME@ filled in)
 *   articles/SLUG.html    article bodies, inserted verbatim except that
 *                         link_headings() gives h2-h6 an id and a self-link
 *   assets/               fonts, images and icons, copied to out/assets/
 *
 * Ownership: every function returning char * returns a heap string owned by
 * the caller. Inside an htmc(...) expression, wrap such a call in own() so
 * the enclosing htmc() frees it once it has been copied into its parent.
 * Parameters typed char * (not const) are taken over the same way.
 */

/* System headers go first: htmc defines function-like macros named after
 * HTML tags (link, select, time, ...) that would clash with their contents. */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "./deps/htmc/htmc.h"

/* ------------------------------------------------------------------ */
/* Articles: add an entry here to publish a new article.              */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *lang; /* ISO 639-1 code of the translation, e.g. "tr" */
    const char *slug; /* slug of the translated article in articles[] */
    /* true when the article at slug was translated from the declaring article
     * with AI: it describes the target page, which then shows a notice. Only
     * one direction of a pair can be true. Defaults to false when left out. */
    bool AI_used;
} ArticleTranslation;

/* A translation is an ordinary entry of articles[] with its own body file.
 * List it in the source's translations (and the source in the translation's,
 * so both pages point at each other), inline and ending with { 0 }:
 *     .translations = (const ArticleTranslation[]){ { .lang = "tr", .slug = "some-slug-tr" }, { 0 } },
 * Add .AI_used = true when that translation was made with AI:
 *     .translations = (const ArticleTranslation[]){ { .lang = "tr", .slug = "some-slug-tr", .AI_used = true }, { 0 } },
 * Positional { "tr", "some-slug-tr" } also means AI_used = false, but -Wextra
 * warns about the missing field; designators don't warn and get completion.
 * main() checks the links at build time. */
typedef struct {
    const char *slug;        /* output file: out/article/<slug>.html */
    const char *lang;        /* ISO 639-1 code of this article, e.g. "en" */
    const char *title;       /* plain text, inserted as-is */
    const char *description; /* shown on the articles list */
    const char *cover;       /* image path under assets/, or NULL */
    const char *cover_alt;
    const char *published;   /* YYYY-MM-DD */
    const char *body_path;   /* pre-escaped HTML fragment, inserted verbatim except for headings */
    const ArticleTranslation *translations; /* ends with { 0 }; NULL when there are none */
} Article;

/* Does the article list at least one translation before its { 0 } terminator? */
static int has_translations(const Article *entry)
{
    return entry->translations && entry->translations[0].slug;
}

static const Article articles[] = {
    {
        .slug = "how-to-run-wasm-in-nextjs",
        .lang = "en",
        .title = "How to run wasm in nextjs?",
        .description = "In this article, I explained how to create a basic Next.js application "
                       "that runs Web assembly (WASM) in a react client component.",
        .cover = NULL,
        .published = "2025-03-28",
        .body_path = "articles/how-to-run-wasm-in-nextjs.html",
    },
    {
        .slug = "how-to-use-ufw-with-docker",
        .lang = "en",
        .title = "How to use UFW with docker",
        .description = "I explained how to use ufw with docker which is basic firewall for linux systems.",
        .cover = "images/article/how-to-use-ufw-with-docker.jpg",
        .cover_alt = "Photo by Stephen Radford — burning house",
        .published = "2025-07-07",
        .body_path = "articles/how-to-use-ufw-with-docker.html",
    },
    {
      .slug = "mathematics-in-the-world-of-software",
      .lang = "en",
      .title = "Mathematics in the World of Software",
      .description = "",
      .cover = "images/article/c-code-3d.png",
      .translations = (const ArticleTranslation[]) {
        {"tr", "yazilim-dunyasinda-matematik", false}, {0}
      },
      .body_path = "articles/Mathematics-in-the-World-of-Software.html",
      .published = "2026-09-07",

    }
    ,{
      .slug = "yazilim-dunyasinda-matematik",
      .lang = "tr",
      .title = "Yazilim dunyasinda matematik",
      .description = "Bir yazılımın doğasında elbette matematik vardır. Yazdığınız kodun önce assembly, ardından binary formatında çalışması, sayısız matematiksel koşul ve işlemin bir araya gelmesiyle...",
      .cover = "images/article/c-code-3d.png",
      .published = "2026-09-07",
      .body_path = "articles/yazilim-dunyasinda-matematik.html",
      .translations = (const ArticleTranslation[]) {
      {"en", "mathematics-in-the-world-of-software", true}, {0}
    }
    },
};

static const size_t article_count = sizeof articles / sizeof articles[0];

typedef struct {
    const char *href;
    const char *icon; /* file name under assets/images/svg/, without .svg */
    const char *alt;
    const char *size; /* Tailwind size classes */
} SocialLink;

static const SocialLink social_links[] = {
    { "mailto:hi@notfound404.dev", "mail", "email", "size-8 md:size-10" },
    { "https://github.com/404alya", "github", "github", "size-7 md:size-9" },
    { "https://stackoverflow.com/users/22740544/404nnotfoundd", "stack-overflow", "stackoverflow", "size-8 md:size-10" },
    { "https://x.com/404nnotfounddd", "xtwitter", "twitter", "size-6 md:size-8" },
};

static const size_t social_link_count = sizeof social_links / sizeof social_links[0];

/* ------------------------------------------------------------------ */
/* Experience: add an entry here to list a new job on the home page.  */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *role;
    const char *company;
    const char *employment;    /* "Full-time", "Contract", ... */
    const char *start;         /* YYYY-MM, read by experience.js for the duration */
    const char *end;           /* YYYY-MM, or NULL while the job is current */
    const char *dates;         /* shown as-is, e.g. "Sep 2025 – Present" */
    const char *location;
    const char *logo;          /* video path under assets/, black on transparent, inverted to white by job_card() */
    const char *logo_alt;
    const char *url;           /* company website the card links to, or NULL */
    const char *const *skills; /* NULL-terminated */
} Job;

static const Job jobs[] = {
    {
        .role = "Frontend Developer",
        .company = "VASA",
        .employment = "Full-time",
        .start = "2025-09",
        .end = NULL,
        .dates = "Sep 2025 – Present",
        .location = "London, United Kingdom · Remote",
        .logo = "video/vasa-logo-animated-black.webm",
        .logo_alt = "VASA logo",
        .url = "https://www.vasa.works/",
        .skills = (const char *const[]){ "Front-End Design", "Web Interface Design", NULL },
    },
};

static const size_t job_count = sizeof jobs / sizeof jobs[0];

/* Slides of the gameproject2 section's slider, in display order. The first
 * one is shown before (and without) JS. */
typedef enum { SLIDE_IMAGE, SLIDE_VIDEO } SlideKind;

typedef struct {
    SlideKind kind;
    const char *path; /* under assets/ */
    const char *alt;  /* img alt, or the video's aria-label */
} Slide;

static const Slide gameproject2_slides[] = {
    { .kind = SLIDE_VIDEO, .path = "video/gameproject2.webm", .alt = "gameproject2 gameplay" },
    { .kind = SLIDE_IMAGE, .path = "images/gameproject2/screenshot-1.webp", .alt = "gameproject2 screenshot 1" },
    { .kind = SLIDE_IMAGE, .path = "images/gameproject2/screenshot-2.webp", .alt = "gameproject2 screenshot 2" },
};

static const size_t gameproject2_slide_count = sizeof gameproject2_slides / sizeof gameproject2_slides[0];

/* Site identity, used by the meta description and the JSON-LD data. */
static const char site_name[] = "notfound404.dev";
static const char site_url[] = "https://notfound404.dev/";
static const char site_description[] = "Samet Alpdeger, a self-taught full-stack developer working with the web and Kotlin. Articles on web development, C, compilers and low-level programming.";
static const char author_name[] = "Samet Alpdeger";
static const char author_job_title[] = "Software Engineer";
static const char author_email[] = "hi@notfound404.dev";
/* Profiles listed in JSON-LD sameAs besides the web links in social_links[]. */
static const char *const extra_profiles[] = {
    "https://www.linkedin.com/in/samet-alpdeger-291a132b5/",
};
static const size_t extra_profile_count = sizeof extra_profiles / sizeof extra_profiles[0];

/* ------------------------------------------------------------------ */
/* Build mode: ./output.o --dev (the default) or --prod               */
/* ------------------------------------------------------------------ */

/* Set once by main() from the command line, before any page is built.
 * dev:  relative paths, so out/ works from any static server or directory
 *       (python3 -m http.server in out/).
 * prod: served from the root of site_url by nginx. Pages also get a canonical
 *       URL and link-preview meta (share_meta()), and out/nginx.conf is written. */
static bool production = false;

/* Prefix of every internal URL and asset path on a page `depth` directories
 * below the site root. Prod uses root-absolute paths: nginx serves 404.html
 * for any missing URL, at any depth (/article/x/y), where relative paths would
 * point nowhere. Dev keeps "" and "../" so out/ needs no server root. */
static const char *page_base(int depth)
{
    if (production)
        return "/";
    return depth == 0 ? "" : "../";
}

/* ------------------------------------------------------------------ */
/* Files and memory                                                   */
/* ------------------------------------------------------------------ */

static void die(const char *what)
{
    perror(what);
    exit(EXIT_FAILURE);
}

/* Like die(), for errors in the site's own data, where errno means nothing. */
static void die_msg(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fputs("error: ", stderr);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
    exit(EXIT_FAILURE);
}

/* Hands a heap string (e.g. a component's output) to the htmc() call being
 * built. htmc frees or recycles it once it has been copied into the parent. */
static char *htmc_adopt(HtmcAllocations *ha, char *str)
{
    size_t idx = htmc_get_unused(ha, 1);
    free(ha->buffers[idx]);
    ha->buffers[idx] = str;
    ha->sizes[idx] = strlen(str);
    ha->caps[idx] = ha->sizes[idx] + 1;
    return str;
}

#define own(str) htmc_adopt(&htmc_ha, (str))

/* Returns a heap string formatted like printf, sized to fit. Exits on failure. */
static char *format_string(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (len < 0)
        die("vsnprintf");

    char *str = malloc((size_t)len + 1);
    if (!str)
        die("malloc");

    va_start(args, fmt);
    vsnprintf(str, (size_t)len + 1, fmt, args);
    va_end(args);
    return str;
}

/* Reads a whole file into a heap buffer sized to it, NUL-terminated so text
 * files can be used as strings. Stores the byte count in *len if len isn't
 * NULL. Exits on failure. */
static char *read_file_sized(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        die(path);

    if (fseek(f, 0, SEEK_END) != 0)
        die(path);
    long size = ftell(f);
    if (size < 0)
        die(path);
    rewind(f);

    char *buf = malloc((size_t)size + 1);
    if (!buf)
        die("malloc");

    size_t n = fread(buf, 1, (size_t)size, f);
    if (ferror(f))
        die(path);
    buf[n] = '\0';
    fclose(f);

    if (len)
        *len = n;
    return buf;
}

static char *read_file(const char *path)
{
    return read_file_sized(path, NULL);
}

/* Growable heap string for output built piece by piece outside htmc (JSON,
 * the heading transform). data is always NUL-terminated and owned by the
 * builder until it is returned; strbuf_new() allocates it up front. */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

static void strbuf_append_n(StrBuf *sb, const char *str, size_t n)
{
    if (sb->len + n + 1 > sb->cap) {
        size_t cap = sb->cap ? sb->cap : 64;
        while (sb->len + n + 1 > cap)
            cap *= 2;
        char *data = realloc(sb->data, cap);
        if (!data)
            die("realloc");
        sb->data = data;
        sb->cap = cap;
    }
    memcpy(sb->data + sb->len, str, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

static void strbuf_append(StrBuf *sb, const char *str)
{
    strbuf_append_n(sb, str, strlen(str));
}

static void strbuf_putc(StrBuf *sb, char c)
{
    strbuf_append_n(sb, &c, 1);
}

static StrBuf strbuf_new(void)
{
    StrBuf sb = { NULL, 0, 0 };
    strbuf_append_n(&sb, "", 0);
    return sb;
}

/* Heap copy of str with ASCII letters uppercased (ISO codes for labels). */
static char *upper_ascii(const char *str)
{
    char *copy = format_string("%s", str);
    for (char *c = copy; *c; c++)
        *c = (char)toupper((unsigned char)*c);
    return copy;
}

/* Heap copy of text with every occurrence of token replaced by value: fills
 * the @NAME@ placeholders of the css/ and config/ files. A plain search, not
 * a format string, so '%' in either is harmless. */
static char *replace_tokens(const char *text, const char *token, const char *value)
{
    StrBuf out = strbuf_new();
    size_t token_len = strlen(token);
    for (const char *found; (found = strstr(text, token)) != NULL; text = found + token_len) {
        strbuf_append_n(&out, text, (size_t)(found - text));
        strbuf_append(&out, value);
    }
    strbuf_append(&out, text);
    return out.data;
}

/* Heap copy of str escaped for a double-quoted HTML attribute value. */
static char *escape_attr(const char *str)
{
    StrBuf out = strbuf_new();
    for (const char *c = str; *c; c++) {
        switch (*c) {
        case '&': strbuf_append(&out, "&amp;"); break;
        case '"': strbuf_append(&out, "&quot;"); break;
        case '<': strbuf_append(&out, "&lt;"); break;
        case '>': strbuf_append(&out, "&gt;"); break;
        default:  strbuf_putc(&out, *c);
        }
    }
    return out.data;
}

/* Writes html to path and frees it. Returns 0 on success. */
static int write_page(const char *path, char *html)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        perror(path);
        free(html);
        return -1;
    }

    int ok = fputs(html, f) != EOF;
    ok = fclose(f) == 0 && ok;
    free(html);

    if (!ok) {
        perror(path);
        return -1;
    }
    printf("wrote %s\n", path);
    return 0;
}

static void make_dir(const char *path)
{
    if (mkdir(path, 0755) != 0 && errno != EEXIST)
        die(path);
}

static int copy_file(const char *src, const char *dst)
{
    size_t len;
    char *data = read_file_sized(src, &len);

    FILE *out = fopen(dst, "wb");
    if (!out) {
        perror(dst);
        free(data);
        return -1;
    }

    int ok = fwrite(data, 1, len, out) == len;
    ok = fclose(out) == 0 && ok;
    free(data);

    if (!ok) {
        perror(dst);
        return -1;
    }
    return 0;
}

/* Recursively copies the directory src to dst. Returns 0 on success. */
static int copy_tree(const char *src, const char *dst)
{
    DIR *dir = opendir(src);
    if (!dir) {
        perror(src);
        return -1;
    }
    make_dir(dst);

    int status = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char *from = format_string("%s/%s", src, entry->d_name);
        char *to = format_string("%s/%s", dst, entry->d_name);

        struct stat st;
        if (stat(from, &st) != 0) {
            perror(from);
            status = -1;
        } else if ((S_ISDIR(st.st_mode) ? copy_tree(from, to) : copy_file(from, to)) != 0) {
            status = -1;
        }

        free(from);
        free(to);
    }
    closedir(dir);
    return status;
}

/* ------------------------------------------------------------------ */
/* Styles and scripts                                                 */
/* ------------------------------------------------------------------ */

/* <script> with a JS file's contents, wrapped in an IIFE so its top-level
 * declarations stay out of the global scope. */
static char *inline_script(const char *path)
{
    return htmc(script("(() => {\n", own(read_file(path)), "\n})();"));
}

/* <style> with a CSS file's contents (taken over), on a new line as the
 * inlined CSS always was. tailwind marks it type="text/tailwindcss", so
 * Tailwind's browser build processes it (@theme, @custom-variant, @slot). */
static char *inline_style(char *css, bool tailwind)
{
    if (tailwind)
        return htmc(attr(style, htmc_strlit(type="text/tailwindcss"))("\n", own(css)));
    return htmc(style("\n", own(css)));
}

/* Tailwind v4 reads its config (css/tailwind-config.css) from
 * <style type="text/tailwindcss">. */
static char *tailwind_config(void)
{
    return inline_style(read_file("css/tailwind-config.css"), true);
}

/* The few rules Tailwind can't express (css/base.css). Font URLs are relative
 * to the page, hence the base path in place of @BASE@. */
static char *base_styles(const char *base)
{
    char *css = read_file("css/base.css");
    char *filled = replace_tokens(css, "@BASE@", base);
    free(css);
    return inline_style(filled, false);
}

/* Stand-in for @tailwindcss/typography, which the Tailwind browser build
 * can't load: the .prose defaults used by the site plus the prose-* variants
 * (css/prose.css). */
static char *prose_styles(void)
{
    return inline_style(read_file("css/prose.css"), true);
}

/* Scripts every page needs. */
static char *global_scripts(void)
{
    return htmc(
        own(tailwind_config()),
        script(own(read_file("deps/js/tailwind.js"))),
        own(inline_script("js/theme.js"))
    );
}

/* ------------------------------------------------------------------ */
/* Linkable headings: ids and styles shared by articles and home      */
/* ------------------------------------------------------------------ */

/* Ids already given out on one page, to number repeats. */
typedef struct {
    char **items;
    size_t count;
    size_t cap;
} IdList;

static int id_list_contains(const IdList *ids, const char *id)
{
    for (size_t n = 0; n < ids->count; n++)
        if (strcmp(ids->items[n], id) == 0)
            return 1;
    return 0;
}

/* Adds id (taken over) to the list. */
static void id_list_add(IdList *ids, char *id)
{
    if (ids->count == ids->cap) {
        size_t cap = ids->cap ? ids->cap * 2 : 16;
        char **items = realloc(ids->items, cap * sizeof *items);
        if (!items)
            die("realloc");
        ids->items = items;
        ids->cap = cap;
    }
    ids->items[ids->count++] = id;
}

static void id_list_free(IdList *ids)
{
    for (size_t n = 0; n < ids->count; n++)
        free(ids->items[n]);
    free(ids->items);
}

/* Turkish letters (UTF-8) and their ASCII stand-ins in heading ids. */
static const struct {
    const char *utf8;
    char ascii;
} id_transliterations[] = {
    { "\xc3\xa7", 'c' }, { "\xc3\x87", 'c' }, /* ç Ç */
    { "\xc4\x9f", 'g' }, { "\xc4\x9e", 'g' }, /* ğ Ğ */
    { "\xc4\xb1", 'i' }, { "\xc4\xb0", 'i' }, /* ı İ */
    { "\xc3\xb6", 'o' }, { "\xc3\x96", 'o' }, /* ö Ö */
    { "\xc5\x9f", 's' }, { "\xc5\x9e", 's' }, /* ş Ş */
    { "\xc3\xbc", 'u' }, { "\xc3\x9c", 'u' }, /* ü Ü */
};

static const size_t id_transliteration_count = sizeof id_transliterations / sizeof id_transliterations[0];

/* Heap id built from a heading's inner HTML: tags are dropped, ASCII letters
 * and digits are kept lowercased, Turkish letters are transliterated, and
 * every other run of characters (entities, other UTF-8, punctuation, spaces)
 * becomes a single '-', never leading or trailing. "section" if nothing is left. */
static char *heading_slug(const char *inner, size_t len)
{
    StrBuf out = strbuf_new();
    int pending_dash = 0;
    size_t n = 0;

    while (n < len) {
        unsigned char c = (unsigned char)inner[n];
        char letter = 0;
        size_t step = 1;

        if (c == '<') {
            while (n < len && inner[n] != '>')
                n++;
            n++;
            continue;
        }
        if (c == '&') {
            while (n < len && inner[n] != ';')
                n++;
        } else if (isalnum(c) && c < 0x80) {
            letter = (char)tolower(c);
        } else if (c >= 0x80) {
            for (size_t t = 0; t < id_transliteration_count; t++)
                if (n + 1 < len && memcmp(inner + n, id_transliterations[t].utf8, 2) == 0)
                    letter = id_transliterations[t].ascii;
            /* Skip the continuation bytes of this UTF-8 sequence. */
            while (n + step < len && ((unsigned char)inner[n + step] & 0xC0) == 0x80)
                step++;
        }
        n += step;

        if (!letter) {
            pending_dash = 1;
            continue;
        }
        if (pending_dash && out.len > 0)
            strbuf_putc(&out, '-');
        pending_dash = 0;
        strbuf_putc(&out, letter);
    }

    if (out.len == 0)
        strbuf_append(&out, "section");
    return out.data;
}

/* Heap copy of base, or base-2, base-3, ... if that is already taken. */
static char *unique_id(const IdList *used, const char *base_id)
{
    char *id = format_string("%s", base_id);
    for (int suffix = 2; id_list_contains(used, id); suffix++) {
        free(id);
        id = format_string("%s-%d", base_id, suffix);
    }
    return id;
}

/* Heading anchors look like the heading (colour, weight, the h2 underline it
 * already has) instead of a .prose link, and show a '#' while hovered. The
 * marker is absolutely positioned so it never takes space or rewraps a
 * heading. scroll-mt keeps a heading clear of the fixed navbar when it's
 * scrolled to, from a click or from a hash in the URL. Used by every .prose
 * container whose headings link to themselves: the article body and the home
 * page's sections. */
static char *heading_link_styles(void)
{
    return htmc(
        attr(style, htmc_strlit(type="text/tailwindcss"))(
            "\n@custom-variant prose-heading-link { & :where(h2, h3, h4, h5, h6) > :where(a[href^='#']) { @slot; } }\n"
        )
    );
}

static const char heading_link_class[] =
    "prose-headings:scroll-mt-24 prose-heading-link:cursor-pointer prose-heading-link:text-inherit! "
    "prose-heading-link:no-underline! prose-heading-link:[font-weight:inherit]! "
    "prose-heading-link:after:absolute prose-heading-link:after:ml-2 prose-heading-link:after:content-['#'] "
    "prose-heading-link:after:opacity-0 prose-heading-link:hover:after:opacity-50 "
    "prose-heading-link:focus-visible:after:opacity-50";

/* Section headings of the home page, in document order. The C components
 * emit them already linked (home_heading()) instead of running
 * link_headings() over the page, and the ids depend on this list alone, so
 * other pages can link to them too (see nav_links()). */
enum { HOME_ABOUT, HOME_EXPERIENCE, HOME_SKILLS, HOME_GAMEPROJECT2, HOME_HEADING_COUNT };

static const char *const home_headings[HOME_HEADING_COUNT] = {
    [HOME_ABOUT] = "Who am I?",
    [HOME_EXPERIENCE] = "Experience",
    [HOME_SKILLS] = "Knowledge Center",
    [HOME_GAMEPROJECT2] = "gameproject2",
};

/* Heap id of a home heading, by the article rules: heading_slug(), and a
 * repeated text gets -2, -3, ... on its later occurrences. */
static char *home_heading_id(size_t index)
{
    IdList ids = { NULL, 0, 0 };
    for (size_t n = 0; n <= index; n++) {
        char *slug = heading_slug(home_headings[n], strlen(home_headings[n]));
        id_list_add(&ids, unique_id(&ids, slug));
        free(slug);
    }
    char *id = format_string("%s", ids.items[index]);
    id_list_free(&ids);
    return id;
}

/* <h2 id="x"><a href="#x">Text</a></h2>, styled by heading_link_class on the
 * enclosing .prose container, like an article heading. */
static char *home_heading(size_t index)
{
    char *id = home_heading_id(index);
    char *heading = htmc(
        attr(h2, htmc_fmt("id=\"%s\"", id))(
            attr(a, htmc_fmt("href=\"#%s\"", id))(htmc_fmt("%s", home_headings[index]))
        )
    );
    free(id);
    return heading;
}

/* ------------------------------------------------------------------ */
/* Shared components                                                  */
/* ------------------------------------------------------------------ */

/* Appends value as a quoted JSON string. Quotes, backslashes and control
 * characters are escaped; '<' too, so a "</script>" in the data can't close
 * the block. Other bytes, UTF-8 included, are valid JSON as they are. */
static void json_append_string(StrBuf *out, const char *value)
{
    strbuf_putc(out, '"');
    for (const unsigned char *c = (const unsigned char *)value; *c; c++) {
        switch (*c) {
        case '"':  strbuf_append(out, "\\\""); break;
        case '\\': strbuf_append(out, "\\\\"); break;
        case '\n': strbuf_append(out, "\\n"); break;
        case '\r': strbuf_append(out, "\\r"); break;
        case '\t': strbuf_append(out, "\\t"); break;
        case '<':  strbuf_append(out, "\\u003c"); break;
        default:
            if (*c < 0x20) {
                char *escaped = format_string("\\u%04x", *c);
                strbuf_append(out, escaped);
                free(escaped);
            } else {
                strbuf_putc(out, (char)*c);
            }
        }
    }
    strbuf_putc(out, '"');
}

/* Appends `,\n      "key": "value"`, the member layout of the @graph nodes. */
static void json_member(StrBuf *out, const char *key, const char *value)
{
    strbuf_append(out, ",\n      ");
    json_append_string(out, key);
    strbuf_append(out, ": ");
    json_append_string(out, value);
}

/* Appends `,\n      "key": { "@id": "id" }`, a reference to another node. */
static void json_member_ref(StrBuf *out, const char *key, const char *id)
{
    strbuf_append(out, ",\n      ");
    json_append_string(out, key);
    strbuf_append(out, ": { \"@id\": ");
    json_append_string(out, id);
    strbuf_append(out, " }");
}

/* Absolute URL of an article page, also its JSON-LD @id. */
static char *article_url(const char *slug)
{
    return format_string("%sarticle/%s.html", site_url, slug);
}

/* schema.org BlogPosting node for an article page, built only from its
 * articles[] entry and the site constants. Every value goes through the JSON
 * escaper, since article text is data. Linked to the WebSite and Person
 * nodes of json_ld() by @id. */
static char *article_json_ld(const Article *entry)
{
    char *url = article_url(entry->slug);
    char *person_id = format_string("%s#person", site_url);
    char *website_id = format_string("%s#website", site_url);

    StrBuf out = strbuf_new();
    strbuf_append(&out, "    {\n      \"@type\": \"BlogPosting\"");
    json_member(&out, "@id", url);
    json_member(&out, "url", url);
    json_member(&out, "mainEntityOfPage", url);
    json_member(&out, "headline", entry->title);
    json_member(&out, "description", entry->description);
    json_member(&out, "datePublished", entry->published);
    json_member(&out, "inLanguage", entry->lang);
    if (entry->cover) {
        char *image = format_string("%sassets/%s", site_url, entry->cover);
        json_member(&out, "image", image);
        free(image);
    }
    json_member_ref(&out, "author", person_id);
    json_member_ref(&out, "publisher", person_id);
    json_member_ref(&out, "isPartOf", website_id);
    if (has_translations(entry)) {
        strbuf_append(&out, ",\n      \"workTranslation\": [");
        for (const ArticleTranslation *translation = entry->translations; translation->slug; translation++) {
            char *translation_url = article_url(translation->slug);
            strbuf_append(&out, translation > entry->translations ? ",\n        { \"@id\": " : "\n        { \"@id\": ");
            json_append_string(&out, translation_url);
            strbuf_append(&out, " }");
            free(translation_url);
        }
        strbuf_append(&out, "\n      ]");
    }
    strbuf_append(&out, "\n    }");

    free(url);
    free(person_id);
    free(website_id);
    return out.data;
}

/* schema.org structured data for search engines: the website and the person
 * behind it on every page, plus extra_node (taken over, may be NULL) as a
 * third @graph node, e.g. an article's BlogPosting. The site values are
 * trusted constants, so they are inserted without JSON escaping. */
static char *json_ld(char *extra_node)
{
    /* Without an extra node the output stays byte-for-byte what it was. */
    char *graph_tail = extra_node ? format_string(",\n%s", extra_node) : NULL;
    free(extra_node);

    return htmc(
        attr(script, htmc_strlit(type="application/ld+json"))(
            htmc_fmt(
                "\n{\n"
                "  \"@context\": \"https://schema.org\",\n"
                "  \"@graph\": [\n"
                "    {\n"
                "      \"@type\": \"WebSite\",\n"
                "      \"@id\": \"%s#website\",\n"
                "      \"url\": \"%s\",\n"
                "      \"name\": \"%s\",\n"
                "      \"description\": \"%s\",\n"
                "      \"publisher\": { \"@id\": \"%s#person\" }\n"
                "    },\n"
                "    {\n"
                "      \"@type\": \"Person\",\n"
                "      \"@id\": \"%s#person\",\n"
                "      \"name\": \"%s\",\n"
                "      \"url\": \"%s\",\n"
                "      \"jobTitle\": \"%s\",\n"
                "      \"email\": \"mailto:%s\",\n"
                "      \"sameAs\": [",
                site_url, site_url, site_name, site_description, site_url,
                site_url, author_name, site_url, author_job_title, author_email),
            htmc_ccode(
                const char *separator = "\n        ";
                for (size_t n = 0; n < extra_profile_count; n++) {
                    htmc_yield(htmc_fmt("%s\"%s\"", separator, extra_profiles[n]));
                    separator = ",\n        ";
                }
                for (size_t n = 0; n < social_link_count; n++) {
                    if (strncmp(social_links[n].href, "http", 4) != 0)
                        continue; /* mailto: is already the email field */
                    htmc_yield(htmc_fmt("%s\"%s\"", separator, social_links[n].href));
                    separator = ",\n        ";
                }
            ),
            "\n      ]\n"
            "    }",
            graph_tail ? own(graph_tail) : "",
            "\n  ]\n"
            "}\n"
        )
    );
}

/* Tags shared by every <head>. base is "" or "../" depending on page depth.
 * graph_node (taken over, may be NULL) is added to the JSON-LD @graph. */
static char *head_common(const char *page_title, const char *base, char *graph_node)
{
    return htmc(
        meta(htmc_strlit(charset="UTF-8")),
        meta(htmc_strlit(name="viewport" content="width=device-width, initial-scale=1.0")),
        title(htmc_fmt("%s", page_title)),
        link(htmc_strlit(rel="icon"), htmc_fmt("href=\"%sassets/images/png/icon.png\"", base)),
        /* The body font, fetched early instead of after the CSS is parsed. */
        link(htmc_strlit(rel="preload" as="font" type="font/woff2" crossorigin),
             htmc_fmt("href=\"%sassets/fonts/Outfit-latin.woff2\"", base)),
        own(base_styles(base)),
        own(json_ld(graph_node))
    );
}

/* Canonical URL and link-preview tags (Open Graph, Twitter card) for a page.
 * Prod only (see production): they name the public URL, and dev output stays
 * as it was. path is the page's path under site_url ("" for the home page);
 * description and image (a path under assets/) may be NULL or empty. Text is
 * data, so it is escaped.
 *
 * The tags are formatted directly rather than with htmc's meta()/link():
 * htmc_make_tag_with_attrs() (deps/htmc, not ours to change) writes the
 * closing ">\0" one byte past its buffer when the attributes end exactly
 * two bytes short of its capacity, and these lengths vary with the data. */
static char *share_meta(const char *path, const char *og_type, const char *share_title,
                        const char *description, const char *image)
{
    char *url = format_string("%s%s", site_url, path);
    char *image_url = format_string("%sassets/%s", site_url, image ? image : "images/png/icon.png");
    char *title_attr = escape_attr(share_title);
    char *description_attr = escape_attr(description ? description : "");
    char *description_tag = *description_attr
        ? format_string("<meta property=\"og:description\" content=\"%s\">", description_attr)
        : format_string("%s", "");
    /* A cover gets the large preview; the fallback icon is small and square. */
    const char *card = image ? "summary_large_image" : "summary";

    char *tags = format_string(
        "<link rel=\"canonical\" href=\"%s\">"
        "<meta property=\"og:type\" content=\"%s\">"
        "<meta property=\"og:site_name\" content=\"%s\">"
        "<meta property=\"og:title\" content=\"%s\">"
        "%s"
        "<meta property=\"og:url\" content=\"%s\">"
        "<meta property=\"og:image\" content=\"%s\">"
        "<meta name=\"twitter:card\" content=\"%s\">",
        url, og_type, site_name, title_attr, description_tag, url, image_url, card);

    free(url);
    free(image_url);
    free(title_attr);
    free(description_attr);
    free(description_tag);
    return tags;
}

/* Duration of the page fade, shared by the CSS transition and the script. */
static const int page_fade_ms = 300;

/* Full-screen cover that makes pages fade in and out: white, or #202020
 * (the article page's dark background) in the dark theme.
 *
 * On load it hides the page until everything is ready, so the visitor never
 * sees unstyled markup, the theme switching, fonts swapping or images popping
 * in, then fades out: the page fades in. It lifts once the window has loaded
 * (scripts, stylesheets, images), Tailwind has applied its CSS, any promise a
 * component registered has settled, and every font the resulting page uses
 * has loaded, or after 5 seconds at most. Components register a promise with:
 *     document.dispatchEvent(new CustomEvent('page:wait', { detail: promise }));
 *
 * Clicking a link to another page of the site fades the cover back in before
 * navigating: the current page fades out.
 *
 * Styled with plain CSS because Tailwind isn't loaded when it first paints.
 * The behaviour lives in js/page-transition.js, which reads the fade duration
 * from data-fade-ms so the CSS transition and the script share page_fade_ms. */
static char *page_transition(void)
{
    return htmc(
        style(
            htmc_fmt(
                "\n#page-transition { position: fixed; inset: 0; z-index: 2147483647; background: #fff;"
                " transition: opacity %dms ease; }\n"
                "#page-transition.dark { background: #202020; }\n"
                "#page-transition.done { opacity: 0; pointer-events: none; }\n",
                page_fade_ms)
        ),
        noscript(style("#page-transition { display: none; }")),
        attr(div, htmc_strlit(id="page-transition" aria-hidden="true"), htmc_fmt("data-fade-ms=\"%d\"", page_fade_ms))(""),
        own(inline_script("js/page-transition.js"))
    );
}

/* Full document around a page body. lang is the page's ISO 639-1 code for
 * <html lang>. graph_node (an extra JSON-LD node) and head_extra may be NULL. */
static char *page_shell(const char *page_title, const char *base, const char *lang,
                        char *graph_node, char *head_extra, char *body_content)
{
    return htmc(
        htmc_doctypehtml,
        attr(html, htmc_fmt("lang=\"%s\"", lang), htmc_strlit(class="h-full w-full overflow-hidden"))(
            head(
                own(head_common(page_title, base, graph_node)),
                head_extra ? own(head_extra) : ""
            ),
            attr(body, htmc_strlit(class="relative h-full w-full overflow-hidden"))(
                own(page_transition()),
                own(body_content),
                own(global_scripts())
            )
        )
    );
}

static char *cursor_canvas(void)
{
    return htmc(
        attr(canvas, htmc_strlit(id="cursor-canvas"
                                 class="pointer-events-none fixed left-0 top-0 z-[9999] hidden opacity-0 transition-opacity duration-[350ms] ease-out lg:block"))(""),
        own(inline_script("js/cursor.js"))
    );
}

static char *progress_bar(void)
{
    return htmc(
        attr(div, htmc_strlit(class="pointer-events-none fixed left-0 top-0 z-[1000] h-1 w-full"))(
            attr(div, htmc_strlit(id="progress-bar-fill"
                                  class="h-full w-0 bg-[rgba(255,255,255,0.73)] transition-[width] duration-100 ease-linear"))("")
        ),
        own(inline_script("js/progress-bar.js"))
    );
}

/* The icon's rotation follows the `dark` class on <html>, so it animates on
 * every toggle without JS; reduced-motion users get an instant switch. The
 * transition only applies once the page-transition cover has lifted (`.done`),
 * so a saved dark theme applied during load snaps to 180deg instead of spinning.
 * The duration is gated too: transition-property defaults to `all`, so a bare
 * duration would animate the rotation on its own. */
static char *theme_button(const char *base)
{
    return htmc(
        attr(button, htmc_strlit(class="theme-btn cursor-pointer" aria-label="Toggle dark mode"))(
            img(htmc_fmt("src=\"%sassets/images/svg/dark-theme.svg\"", base),
                htmc_strlit(alt="theme" class="size-8 rounded-full bg-white opacity-50 motion-safe:[#page-transition.done~*_&]:transition-transform motion-safe:[#page-transition.done~*_&]:duration-300 motion-safe:[#page-transition.done~*_&]:ease-in-out dark:rotate-180"))
        )
    );
}

/* The "Articles" and "Who am I?" links, shared by the desktop bar and the
 * mobile menu. "Who am I?" goes to the home page's heading of that name; on
 * the home page itself page_transition() scrolls to it without a fade. */
static char *nav_links(const char *base, const char *link_class)
{
    char *about_id = home_heading_id(HOME_ABOUT);
    char *links = htmc(
        attr(a, htmc_fmt("href=\"%sarticles.html\"", base),
             htmc_fmt("class=\"cursor-pointer font-pixelify %s\"", link_class))("Articles"),
        attr(a, htmc_fmt("href=\"%sindex.html#%s\"", base, about_id),
             htmc_fmt("class=\"cursor-pointer font-pixelify %s\"", link_class))(htmc_fmt("%s", home_headings[HOME_ABOUT]))
    );
    free(about_id);
    return links;
}

/* Dropdown under the navbar on narrow screens; nav.js toggles data-open. */
static char *mobile_menu(const char *base)
{
    return htmc(
        attr(div, htmc_strlit(id="mobile-menu"
                              class="absolute left-0 right-0 top-full mt-2 hidden flex-col items-center gap-4 rounded-lg bg-[#2b2b2b] p-4 backdrop-blur-md data-[open]:flex min-[600px]:hidden"))(
            own(nav_links(base, "text-[#ffffff7e] hover:text-white text-base md:text-lg"))
        )
    );
}

/* Floating top bar; nav.js toggles data-hidden while scrolling. */
static char *navbar(const char *base)
{
    return htmc(
        attr(div, htmc_strlit(class="pointer-events-none fixed top-0 z-[45] flex w-full justify-center p-2"))(
            attr(nav, htmc_strlit(id="main-nav"
                                  class="pointer-events-auto relative top-0 flex h-full w-full items-center justify-between rounded-lg bg-[#00000071] px-4 py-3 shadow-[0_0px_20px_0.5px_rgba(0,0,0,0.3)] drop-shadow-md backdrop-blur-xs transition-transform duration-300 ease-[ease] data-[hidden]:-translate-y-24 md:w-[50rem] md:bg-[#00000061] md:backdrop-blur-md"))(
                attr(a, htmc_fmt("href=\"%sindex.html\"", base),
                     htmc_strlit(class="cursor-pointer text-[#ffffff7e] transition-colors hover:text-white font-semibold text-base md:text-lg"))(
                  img(htmc_fmt("src=\"%sassets/images/svg/logo.svg\"", base),
                            htmc_strlit(alt="logo" class="w-14 opacity-90 hover:opacity-60 duration-300"))

      ),
                attr(div, htmc_strlit(class="hidden flex-row items-center gap-5 min-[600px]:flex"))(
                  own(nav_links(base, "text-[#ffffff7e] transition-colors hover:text-white text-base md:text-lg")),
                ),
                attr(div, htmc_strlit(class="hidden flex-row items-center gap-5 min-[600px]:flex"))(
                    own(theme_button(base))
                ),
                attr(div, htmc_strlit(class="flex flex-row items-center gap-3 min-[600px]:hidden"))(
                    own(theme_button(base)),
                    attr(button, htmc_strlit(id="menu-btn" aria-label="Open menu" class="cursor-pointer text-[#ffffff7e] hover:text-white"))(
                        img(htmc_fmt("src=\"%sassets/images/svg/menu.svg\"", base),
                            htmc_strlit(alt="menu" class="h-6 w-6 opacity-75"))
                    )
                ),
                own(mobile_menu(base))
            )
        ),
        own(inline_script("js/nav.js"))
    );
}

/* ------------------------------------------------------------------ */
/* Home page components                                               */
/* ------------------------------------------------------------------ */

/* Viewport-sized background behind the scroll container, themed via dark:. */
static char *background_layer(void)
{
    return htmc(
        attr(div, htmc_strlit(id="bg-layer"
       class="pointer-events-none absolute left-0 top-0 z-0 h-full w-full bg-[#ca81f2] bg-[image:radial-gradient(at_20%_20%,hsl(301.9,63.4%,79.4%)_0px,transparent_50%),radial-gradient(at_40%_20%,hsl(302.4,59.4%,74.5%)_0px,transparent_50%)] md:bg-[#ca81f2] dark:bg-[#9045b5] dark:bg-[image:radial-gradient(at_20%_20%,hsl(285.7,28%,48%)_0px,transparent_50%),radial-gradient(at_40%_20%,hsl(284.9,28%,39%)_0px,transparent_50%)]"))("")
    );
}

/* Pink glow over the top 40rem of the content: a WebGL shader
 * (js/hero-shader.js) that draws the same radial gradients and slowly
 * animates them. #pink-overlay keeps the original CSS gradients as the
 * fallback (no WebGL, shader failure, lost context); the script sets
 * data-shader on it once the canvas has drawn a frame, which removes them. */
static char *hero_shader(void)
{
    return htmc(
        attr(div, htmc_strlit(id="pink-overlay" aria-hidden="true"
                              class="pointer-events-none absolute left-0 top-0 z-[-1] flex h-[40rem] w-full bg-[image:radial-gradient(at_100%_0%,#ffabbc_0px,transparent_50%),radial-gradient(at_20%_20%,#eda4b2_0px,transparent_50%)] dark:bg-[image:radial-gradient(at_100%_0%,#cf6e81_0px,transparent_50%),radial-gradient(at_20%_20%,#cf7789_0px,transparent_50%)] min-[601px]:bg-[image:radial-gradient(at_100%_0%,#ffabbc_0px,transparent_50%),radial-gradient(at_0%_0%,#ffffffd3_0px,transparent_50%),radial-gradient(at_30%_0%,#eda4b2_0px,transparent_50%)] dark:min-[601px]:bg-[image:radial-gradient(at_100%_0%,#cf6e81_0px,transparent_50%),radial-gradient(at_0%_0%,#ffffff40_0px,transparent_50%),radial-gradient(at_30%_0%,#cf7789_0px,transparent_50%)] data-[shader]:bg-none!"))(""),
        attr(canvas, htmc_strlit(id="hero-shader" aria-hidden="true"
                                 class="pointer-events-none absolute left-0 top-0 z-[-1] h-[40rem] w-full"))(""),
        own(inline_script("js/hero-shader.js"))
    );
}

static char *hero_title(void)
{
    return htmc(
        attr(h1, htmc_strlit(class="relative z-10 flex min-h-[10rem] md:max-w-[35rem] items-end text-center font-pixelify text-4xl sm:text-5xl sm:leading-10 max-w-[25rem] px-3 text-white md:text-6xl md:leading-12"))(
            "Reallocation of computer science."
        )
    );
}

static char *social_link(const SocialLink *link, const char *base)
{
    const char *target = strncmp(link->href, "http", 4) == 0
        ? " target=\"_blank\" rel=\"noopener noreferrer\""
        : "";

    return htmc(
        attr(a, htmc_fmt("href=\"%s\"%s", link->href, target),
             htmc_strlit(class="flex cursor-pointer items-center duration-200 hover:opacity-60"))(
            img(htmc_fmt("src=\"%sassets/images/svg/%s.svg\" alt=\"%s\" class=\"%s\"", base, link->icon, link->alt, link->size))
        )
    );
}

/* "Who am I?", the mail button and the social icons. */
static char *hero_links(const char *base)
{
    return htmc(

            attr(div, htmc_strlit(class="flex flex-col flex-wrap items-center justify-center gap-5 mt-20 rounded-lg text-sm text-[#9d6d81a4] md:text-base"))(
                attr(div, htmc_strlit(class="flex flex-row flex-wrap justify-center gap-3"))(
                    attr(a, htmc_strlit(href="mailto:hi@notfound404.dev"
                                        class="flex cursor-pointer rounded-md bg-pink-200 px-3 py-2 md:px-4 md:py-2 active:scale-90 text-base md:text-xl font-[600] drop-shadow-[0_0px_4px_#ffcae0] duration-200 hover:opacity-60"))(
                        "Click to send a mail"
                    )
                ),
                attr(div, htmc_strlit(class="flex flex-row flex-wrap items-center justify-center gap-3 rounded-lg px-10 text-sm text-white md:text-base"))(
                    htmc_ccode(
                        for (size_t n = 0; n < social_link_count; n++) {
                            if (n > 0)
                                htmc_yield(attr(span, htmc_strlit(class="hidden text-white/50 md:block text-2xl font-medium"))("|"));
                            htmc_yield(own(social_link(&social_links[n], base)));
                        }
                    )
                )
            )

    );
}

/* Classes shared by the prose sections of the home page. */
static const char home_prose_class[] =
    "prose w-full max-w-4xl px-2 py-2 text-base leading-6 md:text-xl text-[rgba(255,255,255,0.7)] "
    "prose-headings:text-[#ffffffdd] prose-p:opacity-90 prose-strong:text-[#ffffffc0] prose-li:text-[#ffffffa0]";

static char *description_section(void)
{
    return htmc(
        attr(div, htmc_fmt("class=\"%s %s pt-10\"", home_prose_class, heading_link_class))(
            attr(div, htmc_strlit(class="w-full p-2"))(
                own(home_heading(HOME_ABOUT)),
                p("I'm a 20 year old self-taught fullstack ", strong("Web"),
                  " developer. I can work with a lot of languages and frameworks, but I focus on the Web ecosystem. "
                  "I build mobile apps, websites, desktop apps and backend services."),
                p("I also really enjoy learning low-level stuff like the C programming language, compilers, graphics "
                  "APIs and 3D rendering libraries. I don't use these to make money and I'm not an expert in them yet; "
                  "I just genuinely enjoy learning them. For me, coding isn't only about making money. I'm always "
                  "curious about the technical and scientific side of programming."),
                p("I believe in real coding experience and hands-on learning. Real learning comes from understanding "
                  "what you're actually doing. It's not about the tools you use; it's about understanding the math and "
                  "the methods behind them."),
                p("I like learning new things, listening to music, playing games, reading, drawing and so on. I'm "
                  "always open to new opportunities and collaborations. If you want to reach me, you can find my "
                  "social media accounts and email above.")
            )
        )
    );
}

static char *category_dropdown(void)
{
    return htmc(
        attr(div, htmc_strlit(id="category-dropdown-wrap" class="relative inline-block text-[#00000091]"))(
            attr(button, htmc_strlit(id="category-dropdown-btn"
                                     class="flex cursor-pointer flex-row items-center justify-between gap-2 rounded-lg border-2 border-white/50 bg-[#ffffff90] px-3 py-1 text-base outline-hidden transition-all duration-200 hover:shadow-[0_0px_20px_1px_rgba(0,0,0,0.1)] "))(
                attr(span, htmc_strlit(id="category-label"))("All"),
                "<svg id=\"category-arrow\" class=\"h-4 w-4 transition-transform data-[open]:rotate-180\" fill=\"none\" "
                "stroke=\"currentColor\" viewBox=\"0 0 24 24\">"
                "<path stroke-linecap=\"round\" stroke-linejoin=\"round\" stroke-width=\"2\" d=\"M19 9l-7 7-7-7\"/></svg>"
            ),
            attr(div, htmc_strlit(id="category-dropdown-menu"
                                  class="absolute z-10 mt-2 hidden max-h-60 min-w-full overflow-auto rounded-lg border border-gray-200 bg-[#fffffff5] shadow-lg backdrop-blur-xs"))("")
        )
    );
}

static char *shrink_button(void)
{
    return htmc(
        attr(button, htmc_strlit(id="shrink-btn"
                                 class="flex cursor-pointer flex-row items-center justify-center gap-2 rounded-md bg-[#ffffff24] px-2 py-1 text-sm text-white hover:bg-[#ffffff42] md:text-base data-[hidden]:hidden"))(
            attr(span, htmc_strlit(id="shrink-label"))("Expand"),
            "<svg id=\"shrink-icon\" class=\"h-4 w-4 text-white opacity-70\" fill=\"none\" stroke=\"currentColor\" "
            "stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" viewBox=\"0 0 24 24\">"
            "<polyline points=\"4 14 10 14 10 20\"/><polyline points=\"20 10 14 10 14 4\"/>"
            "<line x1=\"10\" y1=\"14\" x2=\"3\" y2=\"21\"/><line x1=\"21\" y1=\"3\" x2=\"14\" y2=\"10\"/></svg>"
        )
    );
}

static char *last_updated_badge(void)
{
    return htmc(
        attr(div, htmc_strlit(class="flex items-center gap-2 rounded bg-[#ffffff3c] px-2 py-1 text-white md:text-base text-sm"))(
            attr(div, htmc_strlit(class="relative size-3"))(
                attr(div, htmc_strlit(class="h-full aspect-square w-full shrink-0 rounded-full bg-white"))(""),
                attr(div, htmc_strlit(class="absolute left-0 top-0 aspect-square h-full w-full shrink-0 animate-live rounded-full bg-white will-change-transform motion-reduce:animate-none"))("")
            ),
            attr(span, htmc_strlit(id="last-updated-text"))("Last updated: calculating…")
        )
    );
}

/* Logo and text of a job card. The logo box has a fixed size so the video
 * can't shift the layout when it loads. */
static char *job_card_content(const Job *entry, const char *base)
{
    return htmc(
        attr(div, htmc_strlit(class="flex size-12 shrink-0 items-center justify-center md:size-16"))(
            attr(video, htmc_fmt("src=\"%sassets/%s\" aria-label=\"%s\"", base, entry->logo, entry->logo_alt),
                 htmc_strlit(role="img" autoplay muted loop playsinline preload="auto"
                             class="size-full object-contain invert"))("")
        ),
        attr(div, htmc_strlit(class="flex min-w-0 flex-col gap-1"))(
            attr(h3, htmc_strlit(class="m-0 font-sans text-lg font-bold leading-6 text-white md:text-xl"))(htmc_fmt("%s", entry->role)),
            attr(div, htmc_strlit(class="text-sm text-white md:text-base"))(htmc_fmt("%s · %s", entry->company, entry->employment)),
            attr(div, htmc_strlit(class="text-sm text-white/90 md:text-base"))(
                htmc_fmt("%s", entry->dates),
                attr(span, htmc_fmt("data-job-start=\"%s\" data-job-end=\"%s\"", entry->start, entry->end ? entry->end : ""),
                     htmc_strlit(class="empty:hidden before:mx-1 before:content-['·']"))("")
            ),
            attr(div, htmc_strlit(class="text-sm text-white/90 md:text-base"))(htmc_fmt("%s", entry->location)),
            attr(ul, htmc_strlit(class="m-0 mt-2 flex list-none flex-row flex-wrap gap-2 p-0"))(
                htmc_ccode(
                    for (const char *const *skill = entry->skills; *skill; skill++)
                        htmc_yield(attr(li, htmc_strlit(class="m-0 rounded-sm bg-[#0000000d] px-2 py-0.5 text-sm text-white md:text-base"))(htmc_fmt("%s", *skill)));
                )
            )
        )
    );
}

static const char job_card_class[] =
    "flex flex-col sm:flex-row items-start gap-3 rounded-md bg-linear-to-br from-white/20 to-white/0 p-3 md:gap-4 md:p-4";

/* Extra classes when the card links to the company. no-underline and
 * font-normal undo the .prose link styles; the gradient stops transition, so
 * the hover brightens smoothly. */
static const char job_card_link_class[] =
    "cursor-pointer font-normal no-underline transition-colors duration-300 hover:from-white/30 hover:to-white/10 "
    "focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-white";

/* One entry of the experience list: a link to the company when it has a url. */
static char *job_card(const Job *entry, const char *base)
{
    if (!entry->url)
        return htmc(attr(div, htmc_fmt("class=\"%s\"", job_card_class))(own(job_card_content(entry, base))));

    return htmc(
        attr(a, htmc_fmt("href=\"%s\" class=\"%s %s\"", entry->url, job_card_class, job_card_link_class),
             htmc_strlit(target="_blank" rel="noopener noreferrer"))(
            own(job_card_content(entry, base))
        )
    );
}

/* Job history; experience.js fills in each job's duration at runtime (and
 * keeps a current job's up to date) so the static build never goes stale. */
static char *experience_section(const char *base)
{
    return htmc(
        attr(div, htmc_fmt("class=\"%s %s\"", home_prose_class, heading_link_class))(
            attr(div, htmc_strlit(class="flex flex-col p-2"))(
                own(home_heading(HOME_EXPERIENCE)),
                attr(div, htmc_strlit(class="flex flex-col gap-3"))(
                    htmc_ccode(
                        for (size_t n = 0; n < job_count; n++)
                            htmc_yield(own(job_card(&jobs[n], base)));
                    )
                )
            )
        ),
        own(inline_script("js/experience.js"))
    );
}

/* Skills grid with its controls and hover tooltip; skills.js renders the icons. */
static char *skills_section(void)
{
    return htmc(
        attr(div, htmc_strlit(id="skill-tooltip" class="pointer-events-none fixed z-50 hidden w-[15rem] max-w-[15rem] md:w-[20rem] md:max-w-[20rem]"))(""),
        attr(div, htmc_fmt("class=\"%s %s pb-10\"", home_prose_class, heading_link_class))(
            attr(div, htmc_strlit(class="flex flex-col gap-5"))(
                attr(div, htmc_strlit(class="flex flex-col p-2"))(
                    own(home_heading(HOME_SKILLS)),
                    attr(p, htmc_strlit(class="max-w-4xl"))(
                        "This isn't a list of everything I've tried for five minutes. I've spent ",
                        strong("at least 20 hours"),
                        " with every technology here, and there are plenty more I chose to leave out. There's still a lot "
                        "I want to learn, and honestly, that's the part I enjoy the most."
                    ),
                    attr(p, htmc_strlit(class="max-w-4xl"))(
                        "Hover over or click an icon to see more about it. The border color shows its category, and the "
                        "workability level and the icon's opacity show how well I know it."
                    )
                ),
                attr(div, htmc_strlit(class="flex flex-col items-center gap-7 md:gap-2"))(
                    attr(div, htmc_strlit(class="flex w-full flex-col justify-between gap-4 rounded-md bg-[#ffffff24] p-2 md:flex-row md:items-center"))(
                        attr(div, htmc_strlit(class="flex h-full flex-row flex-wrap items-center justify-between gap-3"))(
                            own(category_dropdown()),
                            own(shrink_button())
                        ),
                        own(last_updated_badge())
                    ),
                    attr(div, htmc_strlit(id="skills-grid" class="flex flex-row flex-wrap justify-center gap-3"))("")
                )
            )
        ),
        script("(() => {\n", own(read_file("js/skills-data.js")), "\n", own(read_file("js/skills.js")), "\n})();")
    );
}

/* Rotating yellow border: an oversized square conic gradient spins behind the
 * video box and shows only through its 3px padding. The square is 150% of the
 * box width so its corners never come into view at 16:9. Centering uses the
 * translate property (Tailwind v4), so it doesn't clash with the keyframe's
 * transform. Reduced motion gets a static solid border. */
static const char gameproject2_border_class[] =
    "relative overflow-hidden rounded-xl p-[3px] "
    "before:absolute before:left-1/2 before:top-1/2 before:aspect-square before:w-[150%] "
    "before:-translate-x-1/2 before:-translate-y-1/2 before:content-[''] "
    "before:bg-[conic-gradient(from_0deg,var(--color-amber-300),var(--color-yellow-400)_25%,transparent_50%,transparent_75%,var(--color-amber-300))] "
    "before:animate-[gameproject2-spin_4s_linear_infinite] before:will-change-transform "
    "motion-reduce:before:animate-none motion-reduce:before:bg-none motion-reduce:before:bg-yellow-400";

/* Every slide covers the whole 16:9 box; only the one with data-active is
 * shown. Visibility (not display) hides the others, so they can cross-fade
 * and stay out of the accessibility tree. */
static const char gameproject2_slide_class[] =
    "absolute inset-0 m-0 block size-full object-cover invisible opacity-0 "
    "transition-[opacity,visibility] duration-300 motion-reduce:transition-none "
    "data-[active]:visible data-[active]:opacity-100";

static char *gameproject2_slide(const Slide *slide, const char *base, bool active)
{
    const char *active_attr = active ? " data-active" : "";

    /* <img> is a void tag: format it whole (see the htmc void-tag bug in
     * PROMPT-INSTRUCTIONS.md) instead of going through img(). */
    if (slide->kind == SLIDE_IMAGE)
        return format_string("<img src=\"%sassets/%s\" alt=\"%s\" decoding=\"async\" data-gameproject2-slide%s class=\"%s\">",
                             base, slide->path, slide->alt, active_attr, gameproject2_slide_class);

    /* Hidden videos autoplay too; gameproject2-slider.js pauses them until
     * their slide is shown. */
    return htmc(
        attr(video, htmc_fmt("src=\"%sassets/%s\" aria-label=\"%s\" data-gameproject2-slide%s class=\"%s\"",
                             base, slide->path, slide->alt, active_attr, gameproject2_slide_class),
             htmc_strlit(autoplay muted loop playsinline preload="auto"))("")
    );
}

/* Previous/next arrow over one edge of the slider. Hidden until the script
 * runs, so a page without JS just shows the first slide. */
static const char gameproject2_arrow_class[] =
    "absolute top-1/2 z-10 flex size-10 -translate-y-1/2 cursor-pointer items-center justify-center "
    "rounded-full bg-black/50 text-white ring-1 ring-white/30 backdrop-blur-xs "
    "transition-colors duration-200 hover:bg-black/70 "
    "focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-yellow-400 md:size-12";

static char *gameproject2_arrow(bool next)
{
    return htmc(
        attr(button, htmc_fmt("type=\"button\" hidden %s aria-label=\"%s\" class=\"%s %s\"",
                              next ? "data-gameproject2-next" : "data-gameproject2-prev",
                              next ? "Next slide" : "Previous slide",
                              gameproject2_arrow_class, next ? "right-2" : "left-2"))(
            next
                ? "<svg class=\"size-5 md:size-6\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.5\" viewBox=\"0 0 24 24\" aria-hidden=\"true\">"
                  "<path stroke-linecap=\"round\" stroke-linejoin=\"round\" d=\"M9 5l7 7-7 7\"/></svg>"
                : "<svg class=\"size-5 md:size-6\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.5\" viewBox=\"0 0 24 24\" aria-hidden=\"true\">"
                  "<path stroke-linecap=\"round\" stroke-linejoin=\"round\" d=\"M15 5l-7 7 7 7\"/></svg>"
        )
    );
}

/* Slides plus, when there's more than one, the arrows and a screen-reader
 * status. The 16:9 box reserves the height before any media loads, so
 * nothing shifts after the cover lifts or when the slide changes. */
static char *gameproject2_slider(const char *base)
{
    bool multiple = gameproject2_slide_count > 1;
    char *status = format_string("Slide 1 of %zu", gameproject2_slide_count);

    char *html = htmc(
        noscript(style("[data-gameproject2-slide]:not([data-active]) { display: none; }")),
        attr(div, htmc_strlit(data-gameproject2-slider role="region" aria-roledescription="carousel" aria-label="gameproject2 media"
                              class="relative aspect-video w-full overflow-hidden rounded-[calc(var(--radius-xl)-3px)] bg-black/30"))(
            htmc_ccode(
                for (size_t n = 0; n < gameproject2_slide_count; n++)
                    htmc_yield(own(gameproject2_slide(&gameproject2_slides[n], base, n == 0)));
                if (multiple) {
                    htmc_yield(own(gameproject2_arrow(false)));
                    htmc_yield(own(gameproject2_arrow(true)));
                }
            ),
            multiple ? attr(p, htmc_strlit(data-gameproject2-status aria-live="polite" class="sr-only"))(status) : ""
        ),
        multiple ? own(inline_script("js/gameproject2-slider.js")) : ""
    );
    free(status);
    return html;
}

/* Project showcase: text and a slider of gameplay videos and screenshots. */
static char *gameproject2_section(const char *base)
{
    return htmc(
        style("\n@keyframes gameproject2-spin { to { transform: rotate(1turn); } }\n"),
        attr(div, htmc_fmt("class=\"%s %s pb-10\"", home_prose_class, heading_link_class))(
            attr(div, htmc_strlit(class="flex flex-col p-2"))(
                own(home_heading(HOME_GAMEPROJECT2)),
                attr(p, htmc_strlit(class="max-w-4xl"))(
                    "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut labore "
                    "et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut "
                    "aliquip ex ea commodo consequat."
                ),
                attr(p, htmc_strlit(class="max-w-4xl"))(
                    "Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla "
                    "pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit "
                    "anim id est laborum."
                ),
                attr(p, htmc_strlit(class="max-w-4xl"))(
                    "Curabitur pretium tincidunt lacus. Nulla gravida orci a odio, et tempus feugiat. Nullam varius, "
                    "turpis et commodo pharetra, est eros bibendum elit, nec luctus magna felis sollicitudin mauris."
                ),
                attr(div, htmc_fmt("class=\"%s\"", gameproject2_border_class))(
                    own(gameproject2_slider(base))
                )
            )
        )
    );
}

static char *final_words(void)
{
    return htmc(
        attr(a, htmc_strlit(href="https://github.com/404alya/notfound404.dev-new", class="animate-fade cursor-pointer break-keep rounded-md px-2 py-1 text-base font-[400] text-[#ffffffcb] text-center hover:opacity-60 duration-300 md:text-lg drop-shadow-[0_0px_50px_rgba(0,0,0,0.7)]"))(
            "You can see source code of this website →"
        )
    );
}

/* Discord status panel in the bottom-left corner, fed by the Lanyard API.
 * Off the home page for now (see page_home()); kept, without the
 * unused-function warning, until it comes back. */
__attribute__((unused)) static char *presence_widget(const char *base)
{
    return htmc(
        attr(div, htmc_strlit(id="presence-widget" class="fixed bottom-2 left-2 z-30"))(
            attr(button, htmc_strlit(id="openPresenceBtn" aria-label="Show presence"
                                     class="absolute bottom-2 left-2 z-30 max-w-sm animate-fade cursor-pointer space-y-2 rounded-lg bg-[#000000aa] p-1 shadow-lg backdrop-blur-xs duration-300 hover:opacity-0"))(
                img(htmc_fmt("src=\"%sassets/images/svg/up.svg\"", base), htmc_strlit(alt="" class="h-5 w-5 opacity-50"))
            ),
            attr(div, htmc_strlit(id="presenceContent"
                                  class="w-[14rem] max-w-sm rounded-lg bg-gradient-to-tr md:w-[18rem] from-[#000000aa] to-[#4b1f3eaa] p-2 shadow-[0_0px_10px_1px_rgba(0,0,0,0.5)] backdrop-blur-xs"))(
                attr(div, htmc_strlit(class="flex w-full items-center justify-between"))(
                    attr(div, htmc_strlit(class="flex animate-pulse items-center justify-center gap-[0.4rem] p-1"))(
                        attr(div, htmc_strlit(id="presenceDot" class="shrink rounded-full p-[0.35rem] aspect-square"))(""),
                        attr(div, htmc_strlit(id="presencePlatform" class="shrink pt-[0.1rem] text-center text-sm capitalize text-white"))("")
                    ),
                    attr(button, htmc_strlit(id="closePresenceBtn" aria-label="Close" class="flex cursor-pointer items-center opacity-50"))(
                        img(htmc_fmt("src=\"%sassets/images/svg/line.svg\"", base), htmc_strlit(alt="" class="h-6 w-6 rotate-90"))
                    )
                ),
                attr(div, htmc_strlit(id="activities" class="flex flex-col gap-2"))(""),
                attr(div, htmc_strlit(class="break-all p-1 text-sm text-gray-500 "))("What am I doing rn?")
            )
        ),
        own(inline_script("js/presence.js"))
    );
}

/* ------------------------------------------------------------------ */
/* Article components                                                 */
/* ------------------------------------------------------------------ */

static char *article_cover(const Article *entry, const char *base)
{
    return htmc(
        img(htmc_fmt("src=\"%sassets/%s\" alt=\"%s\"", base, entry->cover, entry->cover_alt),
            htmc_strlit(class="my-4 w-full rounded-lg"))
    );
}

/* The articles[] entry with this slug, or NULL. */
static const Article *find_article(const char *slug)
{
    for (size_t n = 0; n < article_count; n++)
        if (strcmp(articles[n].slug, slug) == 0)
            return &articles[n];
    return NULL;
}

/* The link from entry to the article with this slug, or NULL. */
static const ArticleTranslation *find_translation(const Article *entry, const char *slug)
{
    for (const ArticleTranslation *translation = entry->translations; translation && translation->slug; translation++)
        if (strcmp(translation->slug, slug) == 0)
            return translation;
    return NULL;
}

/* Was this article translated with AI? AI_used sits on the source's link to
 * it, so look at every other article's translations. */
static bool is_ai_translated(const Article *entry)
{
    for (size_t n = 0; n < article_count; n++) {
        const ArticleTranslation *translation = find_translation(&articles[n], entry->slug);
        if (translation && translation->AI_used)
            return true;
    }
    return false;
}

/* ---- Translation groups (articles list) ------------------------- */

/* An article plus every article linked to it through translations, in either
 * direction. The articles list shows one card per group. */
typedef struct {
    size_t size;
    const Article *first;    /* alphabetically first slug: the group's id */
    const Article *fallback; /* shown without JS or a matching language */
} ArticleGroup;

static bool linked(const Article *left, const Article *right)
{
    return find_translation(left, right->slug) || find_translation(right, left->slug);
}

/* Is left's slug alphabetically before right's (or right not set yet)? */
static bool slug_before(const Article *left, const Article *right)
{
    return !right || strcmp(left->slug, right->slug) < 0;
}

/* Group membership is computed from articles[] (links followed until nothing
 * new joins), so chains like en -> tr -> de end up in one group. The fallback
 * is the English article, else the source (the one no AI_used link points at),
 * so a group is never left without a visible card. */
static ArticleGroup article_group(const Article *entry)
{
    bool *member = calloc(article_count ? article_count : 1, sizeof *member);
    if (!member)
        die("calloc");
    member[entry - articles] = true;

    for (bool grew = true; grew;) {
        grew = false;
        for (size_t n = 0; n < article_count; n++) {
            if (member[n])
                continue;
            for (size_t m = 0; m < article_count && !member[n]; m++)
                if (member[m] && linked(&articles[m], &articles[n]))
                    member[n] = grew = true;
        }
    }

    ArticleGroup group = { 0 };
    const Article *english = NULL, *source = NULL;
    for (size_t n = 0; n < article_count; n++) {
        if (!member[n])
            continue;
        const Article *candidate = &articles[n];
        group.size++;
        if (slug_before(candidate, group.first))
            group.first = candidate;
        if (strcmp(candidate->lang, "en") == 0 && slug_before(candidate, english))
            english = candidate;
        if (!is_ai_translated(candidate) && slug_before(candidate, source))
            source = candidate;
    }
    free(member);

    group.fallback = english ? english : source ? source : group.first;
    return group;
}

/* ---- Articles list ----------------------------------------------- */

/* A translated article's card carries its group and language for
 * js/article-lang.js; every card but the group's fallback starts hidden, so
 * without JS the list shows one card per group. */
static char *article_card(const Article *entry, const char *base)
{
    ArticleGroup group = article_group(entry);
    char *group_attrs = group.size > 1
        ? format_string(" data-group=\"%s\" data-lang=\"%s\"%s", group.first->slug, entry->lang,
                        entry == group.fallback ? " data-fallback" : " data-hidden")
        : format_string("%s", "");

    char *card = htmc(
        attr(a, htmc_fmt("href=\"%sarticle/%s.html\"%s", base, entry->slug, group_attrs),
             htmc_strlit(class="flex cursor-pointer flex-col gap-3 rounded-md bg-gradient-to-tr from-[#97004b29] to-[#9b045029] p-3 hover:bg-gradient-to-r hover:from-[#97004b54] hover:to-[#751a4829] hover:duration-300 data-[hidden]:hidden"))(
            attr(div, htmc_strlit(class="flex flex-col gap-4"))(
                attr(div, htmc_strlit(class="text-xl font-bold md:text-2xl"))(htmc_fmt("%s", entry->title)),
                attr(p, htmc_strlit(class="text-base md:text-lg"))(htmc_fmt("%s", entry->description))
            ),
            attr(div, htmc_strlit(class="flex flex-row gap-2 text-base md:text-lg"))(div(htmc_fmt("%s", entry->published)))
        )
    );
    free(group_attrs);
    return card;
}

/* Every card stays in the HTML so crawlers see every article; the script
 * picks the visitor's language per group before the page fades in. The
 * noscript rule keeps the build's choice when Tailwind (also JS) can't run. */
static char *articles_list(const Article *const *sorted, const char *base)
{
    return htmc(
        noscript(style("#articles-box [data-hidden] { display: none; }")),
        attr(div, htmc_strlit(id="articles-box"
                              class="flex flex-col gap-2 rounded-lg bg-white p-2 text-black shadow-[0_0px_15px_5px_rgba(0,0,0,0.1)] xxs:w-[90%] md:w-[40rem] dark:bg-gray-700 dark:text-[#ffffffbd]"))(
            htmc_ccode(
                for (size_t n = 0; n < article_count; n++)
                    htmc_yield(own(article_card(sorted[n], base)));
            )
        ),
        own(inline_script("js/article-lang.js"))
    );
}

/* The AI translation notice in the page's language, or NULL when there is no
 * text for it yet (check_translations() refuses to build then). */
static const char *ai_translation_notice_text(const char *lang)
{
    if (strcmp(lang, "en") == 0)
        return "AI technologies were used to translate this article.";
    if (strcmp(lang, "tr") == 0)
        return "Bu makalenin çevirisinde yapay zekâ teknolojileri kullanılmıştır.";
    return NULL;
}

/* Build-time check of the translation links: every article has a lang, and
 * every translation names an existing article in a different language that
 * matches the declared one. Pages would otherwise link to 404s or announce
 * the wrong hreflang. An AI-translated pair is flagged on one side only, and
 * its target's language has a notice text. Exits with a message on the first
 * problem. */
static void check_translations(void)
{
    /* Every lang first: the loop below compares against other entries'. */
    for (size_t n = 0; n < article_count; n++)
        if (!articles[n].lang || !*articles[n].lang)
            die_msg("article \"%s\" has no lang", articles[n].slug);

    for (size_t n = 0; n < article_count; n++) {
        const Article *entry = &articles[n];
        if (!entry->translations)
            continue;

        for (const ArticleTranslation *translation = entry->translations; translation->slug; translation++) {
            if (!translation->lang || !*translation->lang)
                die_msg("article \"%s\": translation \"%s\" has no lang", entry->slug, translation->slug);
            const Article *target = find_article(translation->slug);
            if (!target)
                die_msg("article \"%s\": translation \"%s\" is not an entry of articles[]",
                        entry->slug, translation->slug);
            if (strcmp(translation->lang, entry->lang) == 0)
                die_msg("article \"%s\": translation \"%s\" has the same lang \"%s\" as its source",
                        entry->slug, translation->slug, translation->lang);
            if (strcmp(translation->lang, target->lang) != 0)
                die_msg("article \"%s\": translation \"%s\" is listed as \"%s\" but its lang is \"%s\"",
                        entry->slug, translation->slug, translation->lang, target->lang);
            if (!translation->AI_used)
                continue;
            /* Both sides can't have been translated from each other. */
            const ArticleTranslation *back = find_translation(target, entry->slug);
            if (back && back->AI_used)
                die_msg("articles \"%s\" and \"%s\" both mark each other as AI-translated; "
                        "set AI_used only on the source's link to the translation",
                        entry->slug, target->slug);
            if (!ai_translation_notice_text(target->lang))
                die_msg("article \"%s\" is AI-translated but there is no notice text for lang \"%s\" "
                        "in ai_translation_notice_text()", target->slug, target->lang);
        }
    }
}

/* <link rel="alternate" hreflang> for the article itself and each translation,
 * so search engines can pair the language versions. */
static char *alternate_links(const Article *entry)
{
    return htmc(
        link(htmc_strlit(rel="alternate"),
             htmc_fmt("hreflang=\"%s\" href=\"%sarticle/%s.html\"", entry->lang, site_url, entry->slug)),
        htmc_ccode(
            for (const ArticleTranslation *translation = entry->translations; translation && translation->slug; translation++) {
                htmc_yield(link(htmc_strlit(rel="alternate"),
                                htmc_fmt("hreflang=\"%s\" href=\"%sarticle/%s.html\"",
                                         translation->lang, site_url, translation->slug)));
            }
        )
    );
}

/* Language links under the article title: the current language, then a
 * link to each translation, labelled with the uppercase ISO code. */
static char *language_switcher(const Article *entry, const char *base)
{
    char *current = upper_ascii(entry->lang);

    return htmc(
        attr(nav, htmc_strlit(aria-label="Article languages" class="mb-6 flex flex-row flex-wrap gap-2 text-sm md:text-base"))(
            attr(span, htmc_fmt("lang=\"%s\"", entry->lang),
                 htmc_strlit(aria-current="page" class="rounded-md bg-[#0000001a] px-2 py-0.5 font-semibold text-[#000000c2] dark:bg-[#ffffff26] dark:text-white"))(own(current)),
            htmc_ccode(
                for (const ArticleTranslation *translation = entry->translations; translation && translation->slug; translation++) {
                    htmc_yield(attr(a, htmc_fmt("href=\"%sarticle/%s.html\" hreflang=\"%s\" lang=\"%s\"",
                                                base, translation->slug, translation->lang, translation->lang),
                                    htmc_strlit(class="cursor-pointer rounded-md border border-[#00000033] px-2 py-0.5 font-semibold no-underline transition-opacity duration-200 hover:opacity-60 dark:border-[#ffffff4d]"))(
                        own(upper_ascii(translation->lang))));
                }
            )
        )
    );
}

/* Small muted note under the title of an AI-translated article, in the
 * page's language. Sits closer to the language buttons when they precede it. */
static char *ai_translation_notice(const Article *entry)
{
    return htmc(
        attr(p, htmc_strlit(role="note" class="mb-6 border-l-2 border-[#00000033] pl-3 text-sm font-normal italic text-[#0000008a] md:text-base dark:border-[#ffffff33] dark:text-[#ffffff80] [nav+&]:-mt-3"))(
            htmc_fmt("%s", ai_translation_notice_text(entry->lang))
        )
    );
}

/* A heading element found in an article body: <hN attrs>inner</hN>. */
typedef struct {
    const char *start;     /* '<' of the opening tag */
    const char *attrs;     /* right after "<hN" */
    const char *inner;     /* right after the opening tag's '>' */
    const char *close;     /* '<' of "</hN>" */
    const char *end;       /* right after "</hN>" */
    const char *id;        /* value of an existing id attribute, or NULL */
    size_t id_len;
} Heading;

static int is_tag_name_end(char c)
{
    return c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/* Finds the id attribute in an opening tag's attributes [attrs, attrs_end). */
static const char *find_id_attr(const char *attrs, const char *attrs_end, size_t *len)
{
    const char *c = attrs;
    while (c < attrs_end) {
        while (c < attrs_end && (isspace((unsigned char)*c) || *c == '/'))
            c++;
        const char *name = c;
        while (c < attrs_end && !isspace((unsigned char)*c) && *c != '=' && *c != '/')
            c++;
        size_t name_len = (size_t)(c - name);
        if (name_len == 0)
            break;

        const char *value = NULL;
        size_t value_len = 0;
        if (c < attrs_end && *c == '=') {
            c++;
            if (c < attrs_end && (*c == '"' || *c == '\'')) {
                char quote = *c++;
                value = c;
                while (c < attrs_end && *c != quote)
                    c++;
                value_len = (size_t)(c - value);
                if (c < attrs_end)
                    c++;
            } else {
                value = c;
                while (c < attrs_end && !isspace((unsigned char)*c))
                    c++;
                value_len = (size_t)(c - value);
            }
        }
        if (name_len == 2 && strncasecmp(name, "id", 2) == 0 && value && value_len > 0) {
            *len = value_len;
            return value;
        }
    }
    return NULL;
}

/* If an <h2>..<h6> element starts at s, fills *heading and returns 1. */
static int match_heading(const char *s, Heading *heading)
{
    if (s[0] != '<' || (s[1] != 'h' && s[1] != 'H') || s[2] < '2' || s[2] > '6' || !is_tag_name_end(s[3]))
        return 0;

    /* End of the opening tag, skipping '>' inside quoted attribute values. */
    const char *c = s + 3;
    char quote = 0;
    for (; *c && (quote || *c != '>'); c++) {
        if (quote && *c == quote)
            quote = 0;
        else if (!quote && (*c == '"' || *c == '\''))
            quote = *c;
    }
    if (!*c)
        return 0;

    const char *inner = c + 1;
    for (const char *close = inner; (close = strstr(close, "</")) != NULL; close += 2) {
        if ((close[2] == 'h' || close[2] == 'H') && close[3] == s[2] && is_tag_name_end(close[4])) {
            const char *gt = strchr(close + 4, '>');
            if (!gt)
                return 0;
            heading->start = s;
            heading->attrs = s + 3;
            heading->inner = inner;
            heading->close = close;
            heading->end = gt + 1;
            heading->id = find_id_attr(s + 3, c, &heading->id_len);
            return 1;
        }
    }
    return 0;
}

typedef void (*HeadingVisitor)(const Heading *heading, void *ctx);
typedef void (*GapVisitor)(const char *from, size_t len, void *ctx);

/* Calls visit for every <h2>..<h6> of html outside HTML comments, in order,
 * and visit_gap (if not NULL) for the bytes before, between and after them. */
static void for_each_heading(const char *html, HeadingVisitor visit, GapVisitor visit_gap, void *ctx)
{
    const char *gap = html;
    const char *c = html;
    while ((c = strchr(c, '<')) != NULL) {
        if (strncmp(c, "<!--", 4) == 0) {
            const char *comment_end = strstr(c + 4, "-->");
            if (!comment_end)
                break;
            c = comment_end + 3;
            continue;
        }
        Heading heading;
        if (!match_heading(c, &heading)) {
            c++;
            continue;
        }
        if (visit_gap)
            visit_gap(gap, (size_t)(heading.start - gap), ctx);
        visit(&heading, ctx);
        gap = c = heading.end;
    }
    if (visit_gap)
        visit_gap(gap, strlen(gap), ctx);
}

typedef struct {
    StrBuf out;
    IdList ids;
} HeadingTransform;

static void collect_existing_id(const Heading *heading, void *ctx)
{
    HeadingTransform *state = ctx;
    if (heading->id) {
        char *id = malloc(heading->id_len + 1);
        if (!id)
            die("malloc");
        memcpy(id, heading->id, heading->id_len);
        id[heading->id_len] = '\0';
        id_list_add(&state->ids, id);
    }
}

static void copy_gap(const char *from, size_t len, void *ctx)
{
    HeadingTransform *state = ctx;
    strbuf_append_n(&state->out, from, len);
}

/* Does the heading's content already hold a link? Links can't nest. */
static int has_link(const char *inner, const char *close)
{
    for (const char *c = inner; c + 2 < close; c++)
        if (c[0] == '<' && (c[1] == 'a' || c[1] == 'A') && is_tag_name_end(c[2]))
            return 1;
    return 0;
}

static void link_heading(const Heading *heading, void *ctx)
{
    HeadingTransform *state = ctx;
    StrBuf *out = &state->out;
    char *id;

    /* "<hN", then the generated id (unless it has one), then the original
     * attributes and '>' as they were. */
    strbuf_append_n(out, heading->start, (size_t)(heading->attrs - heading->start));
    if (heading->id) {
        id = malloc(heading->id_len + 1);
        if (!id)
            die("malloc");
        memcpy(id, heading->id, heading->id_len);
        id[heading->id_len] = '\0';
    } else {
        char *slug = heading_slug(heading->inner, (size_t)(heading->close - heading->inner));
        id = unique_id(&state->ids, slug);
        free(slug);
        id_list_add(&state->ids, format_string("%s", id));
        strbuf_append(out, " id=\"");
        strbuf_append(out, id);
        strbuf_putc(out, '"');
    }
    strbuf_append_n(out, heading->attrs, (size_t)(heading->inner - heading->attrs));

    int wrap = !has_link(heading->inner, heading->close);
    if (wrap) {
        strbuf_append(out, "<a href=\"#");
        strbuf_append(out, id);
        strbuf_append(out, "\">");
    }
    strbuf_append_n(out, heading->inner, (size_t)(heading->close - heading->inner));
    if (wrap)
        strbuf_append(out, "</a>");
    strbuf_append_n(out, heading->close, (size_t)(heading->end - heading->close));
    free(id);
}

/* The article body with every <h2>..<h6> given an id and its content wrapped
 * in a link to itself, so sections can be shared by URL:
 *     <h2 id="x"><a href="#x">Text</a></h2>
 * Existing ids are kept (and reserved first, so generated ones never clash).
 * Every byte outside the heading tags is copied unchanged: code blocks depend
 * on exact whitespace. Returns a heap string. */
static char *link_headings(const char *html)
{
    HeadingTransform state = { strbuf_new(), { NULL, 0, 0 } };
    for_each_heading(html, collect_existing_id, NULL, &state);
    for_each_heading(html, link_heading, copy_gap, &state);
    id_list_free(&state.ids);
    return state.out.data;
}

/* highlight.js 11.9.0 with its GitHub Dark theme, both inlined from deps/.
 * The library shares the IIFE with its init call, so hljs stays out of window. */
static char *code_highlighting(void)
{
    return htmc(
        style(own(read_file("deps/js/highlight-github-dark.min.css"))),
        script("(() => {\n", own(read_file("deps/js/highlight.min.js")), "\nhljs.highlightAll();\n})();")
    );
}

/* ------------------------------------------------------------------ */
/* Pages                                                              */
/* ------------------------------------------------------------------ */

static int page_home(void)
{
    const char *base = page_base(0);
    char *head_extra = htmc(
        meta(htmc_strlit(name="description"), htmc_fmt("content=\"%s\"", site_description)),
        own(prose_styles()),
        own(heading_link_styles()),
        production ? own(share_meta("", "website", site_name, site_description, NULL)) : ""
    );

    char *body = htmc(
        own(cursor_canvas()),
        own(progress_bar()),
        own(navbar(base)),
        attr(div, htmc_strlit(id="scroll-container" data-scroll-root class="h-screen w-screen overflow-y-scroll [scrollbar-width:none]"))(
            attr(div, htmc_strlit(class="flex w-full flex-col justify-center gap-7 selection:bg-[#ffffff54]"))(
                own(background_layer()),
                attr(div, htmc_strlit(class="relative z-10 pt-20"))(
                    own(hero_shader()),
                    attr(div, htmc_strlit(class="flex w-full flex-col items-center"))(
                        own(hero_title()),
                        own(hero_links(base)),
                        own(description_section()),
                        own(experience_section(base)),
                        own(skills_section()),
                        own(gameproject2_section(base))
                    ),
                    attr(div, htmc_strlit(class="flex w-full pt-[10rem] flex-col items-center"))(
                        own(final_words())
                    )
                )
            )
        ),
        own(inline_script("js/heading-links.js")),
        // own(presence_widget(base))
    );

    return write_page("out/index.html", page_shell("notfound404.dev", base, "en", NULL, head_extra, body));
}

/* qsort order for the articles list: newest first. YYYY-MM-DD dates compare
 * correctly as strings; equal dates fall back to the slug so the output
 * doesn't depend on articles[] order. */
static int compare_newest_first(const void *lhs, const void *rhs)
{
    const Article *left = *(const Article *const *)lhs;
    const Article *right = *(const Article *const *)rhs;
    int order = strcmp(right->published, left->published);
    return order != 0 ? order : strcmp(left->slug, right->slug);
}

static int page_articles(void)
{
    const char *base = page_base(0);
    /* Sorted view of articles[], so the source array can stay in any order. */
    const Article **sorted = malloc((article_count ? article_count : 1) * sizeof *sorted);
    if (!sorted)
        die("malloc");
    for (size_t n = 0; n < article_count; n++)
        sorted[n] = &articles[n];
    qsort(sorted, article_count, sizeof *sorted, compare_newest_first);

    char *body = htmc(
        own(cursor_canvas()),
        own(navbar(base)),
        attr(div, htmc_strlit(id="articles-scroll" data-scroll-root class="h-full w-full overflow-y-scroll bg-[#bb2f66] [scrollbar-width:none]"))(
            attr(div, htmc_strlit(class="flex w-full flex-col items-center justify-center gap-4 pb-5 pt-[5rem]"))(
                own(articles_list(sorted, base))
            )
        )
    );
    free(sorted);

    char *head_extra = production ? share_meta("articles.html", "website", "Articles", site_description, NULL) : NULL;
    return write_page("out/articles.html", page_shell("404 - Articles", base, "en", NULL, head_extra, body));
}

static int page_404(void)
{
    char *body = htmc(
        attr(main, htmc_strlit(class="flex h-full w-full items-center justify-center bg-pink-400"))(
            attr(div, htmc_strlit(class="text-4xl font-black text-white md:text-5xl drop-shadow-[0_0px_3px_rgba(0,0,0,0.55)]"))(
                "404 NOT FOUND :)"
            )
        )
    );

    /* No share_meta(): the page stands in for whatever URL was missing. */
    return write_page("out/404.html", page_shell("404 NOT FOUND", page_base(0), "en", NULL, NULL, body));
}

/* Classes of the article's .prose container (the heading link classes are
 * added next to them). */
static const char article_prose_class[] =
    "prose px-3 text-base md:text-lg text-[#000000c2] selection:bg-[#9f004da3] prose-h1:pt-10 prose-blockquote:text-[#000000ab] prose-li:marker:text-[#000000] dark:text-[#ffffffa2] dark:prose-headings:text-white dark:prose-strong:text-white dark:prose-blockquote:text-[#ffffffab] prose-a:cursor-pointer dark:prose-a:text-[#ffffff4b] dark:prose-code:text-[#ffffffd9] dark:prose-li:marker:text-white [&_pre]:bg-transparent! [&_pre]:p-0! [&_pre>code.hljs]:rounded-lg [&_pre>code.hljs]:p-4! [&_pre>code.hljs]:text-sm/6 md:[&_pre>code.hljs]:text-base/7 prose-code:text-sm md:prose-code:text-base";

static int page_article(const Article *entry)
{
    const char *base = page_base(1);
    char *path = format_string("out/article/%s.html", entry->slug);
    char *page_title = format_string("404 - %s", entry->title);
    char *page_path = format_string("article/%s.html", entry->slug);

    char *head_extra = htmc(
        own(prose_styles()),
        own(heading_link_styles()),
        own(alternate_links(entry)),
        production ? own(share_meta(page_path, "article", entry->title, entry->description, entry->cover)) : ""
    );
    free(page_path);

    /* Read and transform the body before the htmc() call below. */
    char *raw_body = read_file(entry->body_path);
    char *article_body = link_headings(raw_body);
    free(raw_body);

    char *body = htmc(
        own(cursor_canvas()),
        own(navbar(base)),
        attr(div, htmc_strlit(id="article-scroll" data-scroll-root
                              class="h-full w-full overflow-x-hidden overflow-y-scroll bg-[#f2f0d4] pb-11 pt-[7rem] selection:bg-[#0000004b] [scrollbar-width:none] dark:bg-[#202020]"))(
            attr(div, htmc_strlit(class="items-center justify-center md:flex"))(
                attr(div, htmc_fmt("class=\"%s %s\"", article_prose_class, heading_link_class))(
                    h1(htmc_fmt("%s", entry->title)),
                    has_translations(entry) ? own(language_switcher(entry, base)) : "",
                    is_ai_translated(entry) ? own(ai_translation_notice(entry)) : "",
                    entry->cover ? own(article_cover(entry, base)) : "",
                    own(article_body)
                )
            )
        ),
        own(code_highlighting()),
        own(inline_script("js/heading-links.js"))
    );

    int status = write_page(path, page_shell(page_title, base, entry->lang, article_json_ld(entry), head_extra, body));
    free(path);
    free(page_title);
    return status;
}

/* ------------------------------------------------------------------ */
/* Production server config                                           */
/* ------------------------------------------------------------------ */

/* out/nginx.conf from the template config/nginx.conf, with @SITE_NAME@
 * replaced by site_name. The template explains its choices. */
static int write_nginx_conf(void)
{
    char *conf = read_file("config/nginx.conf");
    char *filled = replace_tokens(conf, "@SITE_NAME@", site_name);
    free(conf);
    return write_page("out/nginx.conf", filled);
}

/* `./output.o [--dev | --prod]`: dev (the default) unless --prod is given. */
static void parse_args(int argc, char **argv)
{
    for (int n = 1; n < argc; n++) {
        if (strcmp(argv[n], "--prod") == 0)
            production = true;
        else if (strcmp(argv[n], "--dev") == 0)
            production = false;
        else
            die_msg("unknown argument \"%s\"; usage: %s [--dev | --prod]", argv[n], argv[0]);
    }
}

int main(int argc, char **argv)
{
    parse_args(argc, argv);
    check_translations();
    printf("%s build\n", production ? "production" : "development");

    make_dir("out");
    make_dir("out/article");

    int status = copy_tree("assets", "out/assets");
    status |= page_home();
    status |= page_articles();
    status |= page_404();
    for (size_t n = 0; n < article_count; n++)
        status |= page_article(&articles[n]);

    /* Dev never writes the server config, and drops one left by a prod build
     * so it isn't served from out/ by mistake. */
    if (production)
        status |= write_nginx_conf();
    else if (remove("out/nginx.conf") != 0 && errno != ENOENT)
        die("out/nginx.conf");

    return status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
