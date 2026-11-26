#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""A tiny database shaped like wiki-slim.db, for the checks: build-pack.py simplewiki turns it
into a Wikipedia pack of a dozen articles (one of them about 250 KB, one with no text).
  fixture.py OUT.db"""
import sqlite3
import sys

ARTICLES = [
    ("Water", [
        ("", "Water (H2O) is a transparent, tasteless, odourless chemical substance.\n\nIt covers about 71% of Earth's surface.", 0),
        ("Uses of water", "People drink water to live. You can purify water by boiling it.\n\n• Drinking\n• Washing\n• Farming", 2),
        ("Water in the universe", "Much of the universe's water is made as stars form.", 3),
        ("Related pages", "• Water cycle\n• Ice\n• Not an article here", 2),
        ("References", "• ↑ Riddick 1970, Table of Physical Properties.", 2),
        ("Other websites", "• Water on a website", 2),
    ]),
    ("Water cycle", [("", "The water cycle moves water between the sea, the air and the land.", 0),
                     ("Related pages", "• Water\n• Rain", 2)]),
    ("Ice", [("", "Ice is frozen water. It floats on water.", 0), ("Related pages", "• Water", 2)]),
    ("Rain", [("", "Rain is water falling from clouds.", 0), ("Related pages", "• Water cycle\n• Water", 2)]),
    ("Hypothermia", [("", "Hypothermia is when the body gets too cold. Shivering is the first sign.", 0)]),
    ("Hypothesis", [("", "A hypothesis is an idea that can be tested.", 0), ("Related pages", "• Science", 2)]),
    ("Armenian hypothesis", [("", "The Armenian hypothesis is an idea about where languages came from.", 0),
                             ("Related pages", "• Hypothesis", 2)]),
    ("Science", [("", "Science is a way of finding out how the world works, by testing a hypothesis.", 0),
                 ("Related pages", "• Hypothesis", 2)]),
    ("World War II", [("", "World War II was a war from 1939 to 1945. *Stars* and _marks_ stay as written: [1] # not a heading.", 0)]),
    ("Purification", [("", "To purify something is to make it clean. Boiling can purify water.", 0)]),
    ("Long article", [("", ("This is a very long article, written to test long text and wrapping. " * 40 + "\n\n") * 90, 0),
                      ("The end", "The last section of the long article.", 2)]),
    ("Empty article", [("References", "• ↑ only references", 2)]),
    ("Zyxwv specimen " + "with a title far longer than the screen, in the manner of the plant specimen articles, " * 3 + "ending here",
     [("", "An article whose title is about 250 characters long.", 0)]),
]


def main(out):
    db = sqlite3.connect(out)
    db.executescript("""
        DROP TABLE IF EXISTS setup; DROP TABLE IF EXISTS articles; DROP TABLE IF EXISTS sections;
        CREATE TABLE setup(key TEXT PRIMARY KEY, value TEXT DEFAULT '');
        CREATE TABLE articles(id INTEGER PRIMARY KEY, title TEXT NOT NULL, entity TEXT NOT NULL);
        CREATE TABLE sections(id INTEGER PRIMARY KEY AUTOINCREMENT, article_id INTEGER, title TEXT, content TEXT, pow INTEGER DEFAULT 0);
        CREATE INDEX idx_sections_article_id ON sections(article_id);
        INSERT INTO setup VALUES ('language', 'en');
    """)
    for i, (title, secs) in enumerate(ARTICLES):
        db.execute("INSERT INTO articles VALUES (?, ?, ?)", (i + 1, title, f"Q{i + 1}"))
        for st, content, pow_ in secs:
            db.execute("INSERT INTO sections(article_id, title, content, pow) VALUES (?, ?, ?, ?)", (i + 1, st, content, pow_))
    db.commit()


if __name__ == "__main__":
    main(sys.argv[1])
