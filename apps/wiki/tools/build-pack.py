#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Make emuZero Wiki packs (.wpack): one SQLite file per source, read-only on the device.

  build-pack.py survival CONTENT_DIR OUT.wpack     the CYD Survival Wiki guides + FM 21-76
  build-pack.py simplewiki WIKI_SLIM_DB OUT.wpack [--titles-only] [--limit N]
  build-pack.py check PACK...                      the pack checks (exit 1 on a problem)
  build-pack.py same PACK1 PACK2                   do they hold the same? (exit 1 if not)
  build-pack.py fold TEXT                          the search key of TEXT (as the app makes it)

Python standard library only. The same input gives the same file, byte for byte.

Everything becomes Markdown here, so the app has one renderer: links are [text](id:N) (an
article in the same pack) and pictures are ![alt](img:NAME) on a line of their own.
Bodies are zlib-compressed one article at a time. A large pack carries its own word index in
plain tables (the image's libsqlite3 is built without FTS3/4/5): for each word, the ids of the
articles that hold it, as ascending varint deltas. Article ids follow the score (1 = best), so
the first ids that match are the best matches; one to three letters typed are answered from
precomputed top lists. A small pack (the survival guides) is searched in memory by the app.
See WIKI-APP-PLAN.md section 5 in the cardputerzero-os folder.
"""
import math
import os
import re
import sqlite3
import struct
import sys
import zlib

PACK_VERSION = "1"

# ---- the search key: lower-case ASCII, Latin-1 accents off (wiki.c's fold_key does the same) ----
_LAT = "aaaaaaaceeeeiiiidnooooo/ouuuuyty"  # U+00E0..U+00FF; U+00C0..U+00DF map the same


def fold(s):
    out = []
    for ch in s:
        o = ord(ch)
        if 65 <= o <= 90:
            out.append(chr(o + 32))
        elif 0xC0 <= o <= 0xFF:
            l = _LAT[o & 0x1F]
            out.append(ch.lower() if l == "/" else l)
        else:
            out.append(ch)
    return " ".join("".join(out).split())


def words(s):
    """title words as the app splits them: runs of letters and digits, folded"""
    return [w for w in re.split(r"[^0-9a-zÀ-￿]+", fold(s)) if w]


# ---- Markdown helpers ----
_MD_INLINE = re.compile(r"([\\`*\[\]~])")


def md_escape(line):
    """plain text as one Markdown line that shows exactly as written"""
    s = _MD_INLINE.sub(r"\\\1", line)
    s = s.replace("_", "\\_") if re.search(r"(^|\W)_|_(\W|$)", s) else s
    m = re.match(r"^(\s*)(#{1,6} |>|[-+] |\d{1,9}[.)]( |$)|(-\s*){3,}$|(\*\s*){3,}$)", s)
    if m:
        s = m.group(1) + "\\" + s[len(m.group(1)):]
    return s


def new_pack(path, meta):
    if os.path.exists(path):
        os.unlink(path)
    db = sqlite3.connect(path)
    db.executescript("""
        PRAGMA page_size = 4096;
        PRAGMA journal_mode = OFF;
        PRAGMA synchronous = OFF;
        CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT) WITHOUT ROWID;
        CREATE TABLE shelves(id INTEGER PRIMARY KEY, name TEXT, kind TEXT, about TEXT, ord INTEGER);
        CREATE TABLE categories(id INTEGER PRIMARY KEY, slug TEXT, name TEXT, colour TEXT, about TEXT, ord INTEGER);
        CREATE TABLE articles(id INTEGER PRIMARY KEY, shelf INTEGER, category INTEGER, ord INTEGER,
                              title TEXT, key TEXT, slug TEXT, tags TEXT, summary TEXT, blen INTEGER, body BLOB);
        CREATE TABLE images(name TEXT PRIMARY KEY, data BLOB) WITHOUT ROWID;
        CREATE TABLE triage(id TEXT PRIMARY KEY, ord INTEGER, question TEXT, answers TEXT) WITHOUT ROWID;
    """)
    for k, v in sorted(meta.items()):
        db.execute("INSERT INTO meta VALUES (?, ?)", (k, str(v)))
    return db


def put_article(db, aid, shelf, cat, ord_, title, body, slug=None, tags="", summary=""):
    raw = body.encode("utf-8")
    db.execute("INSERT INTO articles VALUES (?,?,?,?,?,?,?,?,?,?,?)",
               (aid, shelf, cat, ord_, title, fold(title), slug, tags, summary, len(raw), zlib.compress(raw, 9)))


def finish(db, path):
    db.execute("CREATE INDEX articles_key ON articles(key)")
    db.commit()
    db.execute("VACUUM")
    db.close()
    print(f"{path}: {os.path.getsize(path) / 1e6:.2f} MB")


# =========================== survival ===========================

def parse_front(text):
    m = re.match(r"---\n(.*?)\n---\n?", text, re.S)
    if not m:
        return {}, text
    fields = {}
    for line in m.group(1).splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            fields[k.strip()] = v.strip()
    return fields, text[m.end():]


def table_to_blocks(lines):
    """a Markdown table, laid out for a narrow screen: each row a bold first cell, then the
    other cells as 'Header: value' items"""
    rows = [[c.strip() for c in l.strip().strip("|").split("|")] for l in lines]
    head, body = rows[0], [r for r in rows[1:] if not re.match(r"^:?-+:?$", r[0] or "-")]
    out = []
    for r in body:
        first = r[0] if "**" in r[0] else f"**{r[0]}**"
        out.append(first)
        for h, c in zip(head[1:], r[1:]):
            out.append(f"- {h}: {c}")
        out.append("")
    return out


def guide_md(body, slugs, problems, name):
    out, lines, i = [], body.strip("\n").splitlines(), 0
    while i < len(lines):
        l = lines[i]
        if l.startswith("|"):
            t = []
            while i < len(lines) and lines[i].startswith("|"):
                t.append(lines[i])
                i += 1
            out.extend(table_to_blocks(t))
            continue
        out.append(l)
        i += 1
    text = re.sub(r"\n{3,}", "\n\n", "\n".join(out)).rstrip() + "\n"

    def img(m):
        return f"![{m.group(1)}](img:{os.path.basename(m.group(2))})"
    text = re.sub(r"!\[([^\]]*)\]\(img/([^)]+)\)", img, text)

    def link(m):
        slug = m.group(2)
        if slug not in slugs:
            problems.append(f"{name}: link to missing guide {slug}")
            return m.group(1)
        return f"[{m.group(1)}](id:{slugs[slug]})"
    text = re.sub(r"(?<!!)\[([^\]]+)\]\(([a-z0-9-]+)\.md\)", link, text)
    return text


SMALL = {"a", "an", "and", "as", "at", "by", "for", "from", "in", "into", "of", "on", "or", "the", "to", "with"}
ACRONYMS = {"NBC", "SERE", "US", "POW", "SOS", "CPR", "IV", "CNS", "RICE", "UHF", "VHF", "FM", "SAR"}


def title_case(s):
    out = []
    for k, w in enumerate(s.split()):
        parts = []
        for p in w.split("-"):
            core = re.sub(r"[^A-Za-z]", "", p)
            if core.upper() in ACRONYMS:
                parts.append(p.upper())
            elif k and p.lower() in SMALL and not parts:
                parts.append(p.lower())
            else:
                parts.append(p[:1].upper() + p[1:].lower())
        out.append("-".join(parts))
    return " ".join(out)


FM_HEADER = re.compile(r"\s*FM 21-76 US ARMY SURVIVAL MANUAL Reprinted as permitted by U\.S\. Department of the Army Page \d+ of 233\s*")


def fm_paragraphs(src):
    files = sorted(f for f in os.listdir(src) if re.match(r"\d{4}\.txt$", f))
    text = "\n\n".join(open(os.path.join(src, f), encoding="utf-8").read() for f in files)
    paras = []
    for p in re.split(r"\n\s*\n", text):
        p = " ".join(p.split())
        if not p:
            continue
        starts = FM_HEADER.match(p)
        rest = FM_HEADER.sub(" ", p).strip()
        if not rest:
            continue
        # a page break inside a paragraph: the text after the page header goes on the paragraph before
        if starts and paras and (rest[0].islower() or not re.search(r"[.!?:\"”)]$", paras[-1])) \
                and not paras[-1].startswith("CHAPTER ") and not rest.startswith("- "):
            paras[-1] += " " + rest
        else:
            paras.append(rest)
    # a heading the PDF glued onto the end of the paragraph before it ("... standards. Water")
    out = []
    for p in paras:
        m = GLUED.match(p)
        if m and not p.startswith("- ") and not m.group(2).endswith("--"):
            out += [m.group(1), m.group(2)]
        else:
            out.append(p)
    return out


GLUED = re.compile(r'^(.*[.!?:"”)])\s+([A-Z][A-Za-z\'-]*(?: [A-Za-z\'-]+){0,4})$')


def is_caps_heading(p):
    letters = [c for c in p if c.isalpha()]
    return letters and p.upper() == p and len(p) < 90 and not p.startswith("- ") and sum(c.isupper() for c in letters) >= 3


def is_sub_heading(p):
    return (len(p) < 60 and p[:1].isupper() and not p.startswith("- ") and not re.search(r"[.:;,!?\-]$", p)
            and len(p.split()) <= 8 and not re.match(r"^\d", p))


def fm_chapters(src, problems):
    paras = fm_paragraphs(src)
    try:
        start = paras.index("CHAPTER 1 - INTRODUCTION")
    except ValueError:
        problems.append("FM 21-76: no CHAPTER 1 heading")
        return []
    chapters, cur, i = [], None, start
    pending_quote = None
    while i < len(paras):
        p = paras[i]
        m = re.match(r"^CHAPTER (\d+) - (.+)$", p)
        if m:
            title = m.group(2).strip()
            if title.endswith(",") and i + 1 < len(paras):
                i += 1
                title += " " + paras[i]
            cur = {"num": int(m.group(1)), "title": title_case(title), "lines": []}
            chapters.append(cur)
            i += 1
            continue
        L = cur["lines"]
        if p in ("WARNING", "CAUTION", "NOTE"):
            pending_quote = p.title()
        elif pending_quote:
            L += [f"> **{pending_quote}:** {md_escape(p.replace('--', '—'))}", ""]
            pending_quote = None
        elif is_caps_heading(p):
            L += [f"## {title_case(p)}", ""]
        elif is_sub_heading(p) and i + 1 < len(paras) and not paras[i + 1].startswith("CHAPTER "):
            L += [f"### {p}", ""]
        elif p.startswith("- "):
            if L and L[-1] == "" and len(L) > 1 and L[-2].startswith("- "):
                L.pop()  # list items stay together
            L += ["- " + md_escape(p[2:].replace("--", "—")), ""]
        else:
            L += [md_escape(p.replace("--", "—")), ""]
        i += 1
    for c in chapters:
        c["body"] = "\n".join(c["lines"]).strip() + "\n"
    return chapters


def parse_triage(path):
    text = open(path, encoding="utf-8").read()
    nodes = []
    # { "id", "question", N, { {"label", "to"}, ... } },
    for m in re.finditer(r'\{"([a-z0-9]+)",\s*"((?:[^"\\]|\\.)*)",\s*(\d+),\s*\{(.*?\})\s*\}\s*\},', text, re.S):
        opts = re.findall(r'\{"((?:[^"\\]|\\.)*)",\s*"([^"]+)"\}', m.group(4))
        nodes.append((m.group(1), m.group(2), opts, int(m.group(3))))
    return nodes


def build_survival(src, out):
    problems = []
    cats = []
    for line in open(os.path.join(src, "categories.tsv"), encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        slug, name, colour, about = line.rstrip("\n").split("\t")
        cats.append((slug, name, colour, about))
    cat_id = {c[0]: i + 1 for i, c in enumerate(cats)}

    guides = []
    adir = os.path.join(src, "articles")
    for f in sorted(os.listdir(adir)):
        if not f.endswith(".md"):
            continue
        front, body = parse_front(open(os.path.join(adir, f), encoding="utf-8").read())
        slug = f[:-3]
        if front.get("category") not in cat_id:
            problems.append(f"{f}: unknown category {front.get('category')}")
            continue
        guides.append((cat_id[front["category"]], int(front.get("order", 99)), front.get("title", slug), slug, front, body))
    guides.sort(key=lambda g: (g[0], g[1], g[2]))
    slugs = {g[3]: i + 1 for i, g in enumerate(guides)}

    db = new_pack(out, {
        "name": "Survival", "version": PACK_VERSION, "index": "memory",
        "source": "CYD Survival Wiki (everyday3d/esp32-cyd-survival-wiki) at 06256dc, and FM 21-76 (US Army, 1992)",
        "licence": "Guides: MIT, Everyday3D / BoxerDawg 3D. FM 21-76: US Army publication, public domain.",
        "notice": "An informational reference guide, not a substitute for professional medical advice, "
                  "certified wilderness survival training, or emergency services.",
        "footer": "From the CYD Survival Wiki · MIT licence · not a substitute for professional help",
        "order": "1",
    })
    db.execute("INSERT INTO shelves VALUES (1, 'Survival guides', 'guides', ?, 1)",
               (f"{len(guides)} guides in {len(cats)} topics",))
    db.execute("INSERT INTO shelves VALUES (2, 'Army Survival Manual', 'book', 'FM 21-76, US Army, 1992', 2)")
    for i, (slug, name, colour, about) in enumerate(cats):
        db.execute("INSERT INTO categories VALUES (?,?,?,?,?,?)", (i + 1, slug, name, colour, about, i + 1))

    used_imgs = set()
    for i, (cid, order, title, slug, front, body) in enumerate(guides):
        md = guide_md(body, slugs, problems, slug + ".md")
        used_imgs.update(re.findall(r"\]\(img:([^)]+)\)", md))
        put_article(db, i + 1, 1, cid, order, title, md, slug=slug, tags=front.get("tags", ""), summary=front.get("summary", ""))

    chapters = fm_chapters(os.path.join(src, "fm21-76"), problems)
    base = len(guides)
    for k, c in enumerate(chapters):
        put_article(db, base + k + 1, 2, None, c["num"], c["title"], c["body"], slug=f"fm21-76-{c['num']}",
                    summary=f"FM 21-76 · Chapter {c['num']}")

    idir = os.path.join(src, "img")
    for f in sorted(os.listdir(idir)):
        if f.endswith(".jpg"):
            db.execute("INSERT INTO images VALUES (?, ?)", (f, open(os.path.join(idir, f), "rb").read()))
    for name in sorted(used_imgs):
        if not os.path.exists(os.path.join(idir, name)):
            problems.append(f"picture {name} is used but missing")

    for k, (nid, q, opts, count) in enumerate(parse_triage(os.path.join(src, "triage.h"))):
        if len(opts) != count:
            problems.append(f"triage {nid}: {len(opts)} answers read, the source says {count}")
        ans = []
        for label, to in opts:
            if to.startswith("g:"):
                if to[2:] not in slugs:
                    problems.append(f"triage {nid}: guide {to[2:]} missing")
                    continue
                to = f"id:{slugs[to[2:]]}"
            elif to == "x:lost":  # the CYD's own "lost" screen (maps, compass): STOP is the guide for it
                to = f"id:{slugs['stop']}"
            ans.append(f"{label}\t{to}")
        db.execute("INSERT INTO triage VALUES (?,?,?,?)", (nid, k, q.replace('\\"', '"'), "\n".join(ans)))
    finish(db, out)
    for p in problems:
        print("problem:", p, file=sys.stderr)
    return 1 if problems else 0


# =========================== Simple English Wikipedia ===========================

DROP = {"references", "other websites", "notes", "sources", "further reading", "external links", "gallery",
        "footnotes", "bibliography", "citations"}
STOP = set("""a an and are as at be but by for from had has have he her his how i if in into is it its
of on or she so than that the their them then there these they this to was we were what when where
which who why will with you your""".split())


def wiki_markdown(sections, title_id):
    """an article's sections as Markdown, with Related pages as links; returns (md, plain text, links)"""
    md, plain, links = [], [], []
    for stitle, content, pow_ in sections:
        st = (stitle or "").strip()
        if st.lower() in DROP:
            continue
        related = st.lower() == "related pages"
        if st:
            md += ["", "#" * min(max(pow_, 2), 4) + " " + md_escape(st), ""]
            plain.append(st)
        for raw in (content or "").split("\n"):
            line = raw.strip()
            if not line:
                if md and md[-1] != "":
                    md.append("")
                continue
            if line.startswith("↑"):
                continue
            plain.append(line)
            if line.startswith("• "):
                item = line[2:].strip()
                if related and fold(item) in title_id:
                    tid = title_id[fold(item)]
                    links.append(tid)
                    md.append(f"- [{md_escape(item)}](id:{tid})")
                else:
                    md.append("- " + md_escape(item))
            else:
                md.append(md_escape(line))
    while md and md[0] == "":
        md.pop(0)
    text = "\n".join(md)
    text = re.sub(r"\n{3,}", "\n\n", text).strip() + "\n"
    return text, "\n".join(plain), links


def build_simplewiki(src, out, titles_only=False, limit=None):
    s = sqlite3.connect(f"file:{src}?mode=ro", uri=True)
    arts = s.execute("SELECT id, title FROM articles ORDER BY id" + (f" LIMIT {int(limit)}" if limit else "")).fetchall()
    keep = {a for a, _ in arts}
    key_of = {}
    for aid, title in arts:
        key_of.setdefault(fold(title), aid)
    print(f"{len(arts)} articles", file=sys.stderr)

    # pass 1: lengths and Related-pages links, for the score
    inbound, length = {}, {}
    cur, buf = None, []

    def sections_of():
        q = s.execute("SELECT article_id, title, content, pow FROM sections ORDER BY article_id, id")
        aid, secs = None, []
        for row in q:
            if row[0] != aid:
                if aid in keep:
                    yield aid, secs
                aid, secs = row[0], []
            secs.append(row[1:])
        if aid in keep:
            yield aid, secs

    for aid, secs in sections_of():
        n = 0
        for st, content, _ in secs:
            if (st or "").strip().lower() in DROP:
                continue
            n += len(content or "")
            if (st or "").strip().lower() == "related pages":
                for line in (content or "").split("\n"):
                    line = line.strip()
                    if line.startswith("• ") and fold(line[2:].strip()) in key_of:
                        t = key_of[fold(line[2:].strip())]
                        if t != aid:
                            inbound[t] = inbound.get(t, 0) + 1
        length[aid] = n
    keep = {a for a in keep if length.get(a, 0) > 0}   # a few source articles have no text at all
    arts = [(a, t) for a, t in arts if a in keep]
    # longer articles and ones other articles point to first; tried on real searches (WIKI-APP-PLAN.md 6.4)
    score = {aid: math.log(1 + length.get(aid, 0)) + math.log(1 + inbound.get(aid, 0)) for aid, _ in arts}
    title_of = dict(arts)
    order = sorted(keep, key=lambda a: (-score[a], fold(title_of[a]), a))
    new_id = {a: i + 1 for i, a in enumerate(order)}
    title_id = {k: new_id[a] for k, a in key_of.items() if a in new_id}

    db = new_pack(out, {
        "name": "Simple English Wikipedia", "version": PACK_VERSION, "index": "words",
        "source": "Simple English Wikipedia, converted from wiki-slim.db (snapshot date unknown)",
        "licence": "Text: CC BY-SA 4.0, Wikipedia contributors (simple.wikipedia.org)",
        "footer": "From Simple English Wikipedia · CC BY-SA 4.0",
        "order": "3", "articles": str(len(arts)), "bodies": "0" if titles_only else "1",
    })
    db.execute("INSERT INTO shelves VALUES (1, 'Wikipedia', 'encyclopedia', ?, 3)", (f"{len(arts):,} articles",))
    db.execute("CREATE TABLE tw(word TEXT PRIMARY KEY, ids BLOB)")   # title words
    db.execute("CREATE TABLE bw(word TEXT PRIMARY KEY, ids BLOB)")   # words anywhere in the article
    db.execute("CREATE TABLE top(prefix TEXT PRIMARY KEY, ids BLOB) WITHOUT ROWID")

    # pass 2: write in id (score) order; sections are read per article through the index
    tops, tw, bw = {}, {}, {}
    for k, a in enumerate(order):
        secs = s.execute("SELECT title, content, pow FROM sections WHERE article_id = ? ORDER BY id", (a,)).fetchall()
        md, plain, _ = wiki_markdown(secs, title_id)
        aid = new_id[a]
        put_article(db, aid, 1, None, 0, title_of[a], md)
        tws = set(words(title_of[a]))
        for w in tws:
            post(tw, w, aid)
        if not titles_only:
            for w in set(words(plain)) | tws:
                if 1 < len(w) <= 40 and not (w.isdigit() and len(w) > 4):
                    post(bw, w, aid)
        tw_list = words(title_of[a])
        seen = set()
        for k2, w in enumerate(tw_list):
            for n in (1, 2, 3):
                p = w[:n]
                if len(p) == n and (p, k2 == 0) not in seen:
                    seen.add((p, k2 == 0))
                    t = tops.setdefault(p, ([], []))[0 if k2 == 0 else 1]
                    if len(t) < 200:
                        t.append((aid * (1 + 0.5 * (len(tw_list) - 1)), aid))
        if k % 20000 == 0:
            print(f"  {k}/{len(order)}  words {len(bw)}", file=sys.stderr)
    for p in sorted(tops):
        # titles that start with p first (longer titles pushed down), then titles with a later word that does
        first = [aid for _, aid in sorted(tops[p][0])][:30]
        rest = [aid for aid in sorted(aid for _, aid in tops[p][1]) if aid not in first]
        ids = (first + rest)[:30]
        db.execute("INSERT INTO top VALUES (?, ?)", (p, struct.pack(f"<{len(ids)}I", *ids)))
    for table, d in (("tw", tw), ("bw", bw)):
        for w in sorted(d):
            db.execute(f"INSERT INTO {table} VALUES (?, ?)", (w, bytes(d[w][0])))
    finish(db, out)
    return 0


def post(index, word, aid):
    """add aid (ascending) to word's list: varint deltas"""
    e = index.get(word)
    if e is None:
        e = index[word] = [bytearray(), 0]
    d = aid - e[1]
    e[1] = aid
    b = e[0]
    while d >= 0x80:
        b.append((d & 0x7F) | 0x80)
        d >>= 7
    b.append(d)


def unpost(blob):
    ids, cur, v, sh = [], 0, 0, 0
    for byte in blob:
        v |= (byte & 0x7F) << sh
        if byte & 0x80:
            sh += 7
            continue
        cur += v
        ids.append(cur)
        v = sh = 0
    return ids


# =========================== checks ===========================

def check(path):
    problems = []
    db = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    meta = dict(db.execute("SELECT key, value FROM meta"))
    for k in ("name", "version", "index", "licence", "footer"):
        if k not in meta:
            problems.append(f"meta {k} missing")
    ids = {r[0] for r in db.execute("SELECT id FROM articles")}
    imgs = {r[0] for r in db.execute("SELECT name FROM images")}
    n = 0
    for aid, title, blen, body in db.execute("SELECT id, title, blen, body FROM articles"):
        n += 1
        if not title:
            problems.append(f"article {aid}: no title")
        text = zlib.decompress(body).decode("utf-8")
        if len(text.encode()) != blen:
            problems.append(f"article {aid}: length {blen} recorded, {len(text.encode())} stored")
        if not text.strip():
            problems.append(f"article {aid} {title}: empty")
        for t in re.findall(r"\]\(id:(\d+)\)", text):
            if int(t) not in ids:
                problems.append(f"article {aid} {title}: link to missing id {t}")
        for t in re.findall(r"\]\(img:([^)]+)\)", text):
            if t not in imgs:
                problems.append(f"article {aid} {title}: missing picture {t}")
        if re.search(r"\]\([a-z0-9-]+\.md\)", text):
            problems.append(f"article {aid} {title}: unconverted .md link")
        if "Page " in text and re.search(r"Page \d+ of 233", text):
            problems.append(f"article {aid} {title}: FM page header left")
    for nid, ans in db.execute("SELECT id, answers FROM triage"):
        nodes = {r[0] for r in db.execute("SELECT id FROM triage")}
        for line in ans.split("\n"):
            label, to = line.split("\t")
            if to.startswith("id:") and int(to[3:]) not in ids:
                problems.append(f"triage {nid}: {label} leads to missing id")
            elif not to.startswith("id:") and to not in nodes:
                problems.append(f"triage {nid}: {label} leads to missing node {to}")
    if meta.get("name") == "Survival":
        g = db.execute("SELECT count(*) FROM articles WHERE shelf = 1").fetchone()[0]
        ch = [r[0] for r in db.execute("SELECT ord FROM articles WHERE shelf = 2 ORDER BY ord")]
        if g != 118:
            problems.append(f"{g} guides, want 118")
        if ch != list(range(1, 24)):
            problems.append(f"FM 21-76 chapters {ch}, want 1..23")
        if db.execute("SELECT count(*) FROM triage").fetchone()[0] != 16:
            problems.append("triage: want 16 questions")
        if db.execute("SELECT count(*) FROM categories").fetchone()[0] != 15:
            problems.append("want 15 categories")
        for slug in ("hypothermia", "fire-lays", "purifying-water", "cpr"):
            if not db.execute("SELECT 1 FROM articles WHERE slug = ?", (slug,)).fetchone():
                problems.append(f"guide {slug} missing")
    if meta.get("index") == "words":
        if not db.execute("SELECT count(*) FROM top").fetchone()[0]:
            problems.append("no top lists")
        w = db.execute("SELECT id FROM articles WHERE key = 'water'").fetchone()
        if not w:
            problems.append("no article Water")
        else:
            row = db.execute("SELECT ids FROM tw WHERE word = 'water'").fetchone()
            if not row or w[0] not in unpost(row[0]):
                problems.append("title word index misses Water")
            if meta.get("bodies") == "1":
                text = zlib.decompress(db.execute("SELECT body FROM articles WHERE id = ?", (w[0],)).fetchone()[0]).decode()
                for word in ("hydrogen", "transparent"):
                    if word in text.lower():
                        row = db.execute("SELECT ids FROM bw WHERE word = ?", (word,)).fetchone()
                        if not row or w[0] not in unpost(row[0]):
                            problems.append(f"text index misses '{word}' in Water")
        for p, blob in db.execute("SELECT prefix, ids FROM top LIMIT 2000"):
            ids = struct.unpack(f"<{len(blob) // 4}I", blob)
            if len(set(ids)) != len(ids) or not ids:
                problems.append(f"top list {p}: empty or repeats")
    print(f"{path}: {n} articles, {len(imgs)} pictures, {len(problems)} problems")
    for p in problems[:50]:
        print("  problem:", p)
    return 1 if problems else 0


def digest(path):
    """a hash of what a pack holds (not of its bytes, which can differ between SQLite versions)"""
    import hashlib
    h = hashlib.sha256()
    db = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    for table, order in (("meta", "key"), ("shelves", "id"), ("categories", "id"), ("articles", "id"),
                         ("images", "name"), ("triage", "id")):
        for row in db.execute(f"SELECT * FROM {table} ORDER BY {order}"):
            for v in row:
                if isinstance(v, bytes) and table == "articles":
                    v = zlib.decompress(v)
                h.update(repr(v).encode() if not isinstance(v, bytes) else v)
                h.update(b"\x00")
    return h.hexdigest()


def main(argv):
    if len(argv) >= 3 and argv[1] == "survival":
        return build_survival(argv[2], argv[3])
    if len(argv) >= 3 and argv[1] == "simplewiki":
        limit = int(argv[argv.index("--limit") + 1]) if "--limit" in argv else None
        return build_simplewiki(argv[2], argv[3], "--titles-only" in argv, limit)
    if len(argv) >= 3 and argv[1] == "check":
        return max(check(p) for p in argv[2:])
    if len(argv) == 3 and argv[1] == "digest":
        print(digest(argv[2]))
        return 0
    if len(argv) == 4 and argv[1] == "same":   # two packs hold the same (exit 1 if not)
        return 0 if digest(argv[2]) == digest(argv[3]) else 1
    if len(argv) == 3 and argv[1] == "fold":
        print(fold(argv[2]))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
