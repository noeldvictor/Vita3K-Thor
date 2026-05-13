#!/usr/bin/env python3
"""SQLite-backed Vita3K Thor debug knowledge base.

The database is the canonical report store for recurring emulator and game
issues. Markdown reports are legacy/export artifacts; new durable notes should
be written here so agents can query by recency, title, platform, shader hash,
and semantic similarity.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import sqlite3
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any


DEFAULT_DB = Path("reports/debug_knowledge.sqlite")
SCHEMA_VERSION = 1
EMBED_DIM = 256
TOKEN_RE = re.compile(r"[a-z0-9_]{2,}", re.IGNORECASE)


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def parse_time(value: str) -> datetime:
    if value.endswith("Z"):
        value = value[:-1] + "+00:00"
    return datetime.fromisoformat(value)


def slugify(value: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")
    return slug or "untitled"


def tokenize(text: str) -> list[str]:
    return [m.group(0).lower() for m in TOKEN_RE.finditer(text)]


def embed_text(text: str) -> dict[str, float]:
    """Sparse signed hashing embedding stored as JSON.

    This is intentionally local and deterministic. It is not as strong as a
    model embedding, but it gives us cheap semantic-ish recall without needing
    network credentials or a SQLite vector extension.
    """

    counts: dict[int, float] = {}
    for token in tokenize(text):
        digest = hashlib.sha256(token.encode("utf-8")).digest()
        bucket = int.from_bytes(digest[:2], "little") % EMBED_DIM
        sign = -1.0 if digest[2] & 1 else 1.0
        counts[bucket] = counts.get(bucket, 0.0) + sign

    norm = math.sqrt(sum(v * v for v in counts.values()))
    if norm == 0:
        return {}
    return {str(k): round(v / norm, 6) for k, v in sorted(counts.items()) if v}


def cosine_sparse(a: dict[str, float], b: dict[str, float]) -> float:
    if not a or not b:
        return 0.0
    if len(a) > len(b):
        a, b = b, a
    return sum(v * b.get(k, 0.0) for k, v in a.items())


def connect(db_path: Path) -> sqlite3.Connection:
    db_path.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row
    return conn


def init_db(conn: sqlite3.Connection) -> None:
    conn.executescript(
        """
        PRAGMA foreign_keys = ON;

        CREATE TABLE IF NOT EXISTS meta (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS cases (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            domain TEXT NOT NULL CHECK (domain IN ('emulator', 'game')),
            title_id TEXT,
            slug TEXT NOT NULL,
            status TEXT NOT NULL DEFAULT 'active',
            severity TEXT NOT NULL DEFAULT 'normal',
            platform_scope TEXT NOT NULL DEFAULT 'windows-android',
            summary TEXT NOT NULL,
            current_hypothesis TEXT NOT NULL DEFAULT '',
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            UNIQUE(domain, title_id, slug)
        );

        CREATE TABLE IF NOT EXISTS entries (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            case_id INTEGER NOT NULL REFERENCES cases(id) ON DELETE CASCADE,
            entry_type TEXT NOT NULL CHECK (entry_type IN (
                'observation', 'decision', 'fix', 'regression',
                'question', 'repro', 'build', 'test', 'note'
            )),
            platform TEXT NOT NULL DEFAULT 'unknown',
            summary TEXT NOT NULL,
            body TEXT NOT NULL DEFAULT '',
            artifact_refs_json TEXT NOT NULL DEFAULT '[]',
            shader_hashes_json TEXT NOT NULL DEFAULT '[]',
            commit_hash TEXT NOT NULL DEFAULT '',
            source TEXT NOT NULL DEFAULT 'codex',
            recency_pin INTEGER NOT NULL DEFAULT 0,
            created_at TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS chunks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            case_id INTEGER NOT NULL REFERENCES cases(id) ON DELETE CASCADE,
            entry_id INTEGER REFERENCES entries(id) ON DELETE CASCADE,
            domain TEXT NOT NULL,
            title_id TEXT,
            chunk_type TEXT NOT NULL,
            source_ref TEXT NOT NULL,
            text TEXT NOT NULL,
            embedding_json TEXT NOT NULL,
            created_at TEXT NOT NULL
        );

        CREATE INDEX IF NOT EXISTS idx_cases_status ON cases(status);
        CREATE INDEX IF NOT EXISTS idx_cases_title ON cases(title_id);
        CREATE INDEX IF NOT EXISTS idx_entries_case ON entries(case_id);
        CREATE INDEX IF NOT EXISTS idx_chunks_case ON chunks(case_id);
        CREATE INDEX IF NOT EXISTS idx_chunks_title ON chunks(title_id);
        CREATE INDEX IF NOT EXISTS idx_chunks_created ON chunks(created_at);
        """
    )

    try:
        conn.execute(
            "CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5(text, source_ref, content='chunks', content_rowid='id')"
        )
        conn.execute("INSERT OR REPLACE INTO meta(key, value) VALUES ('fts5', '1')")
    except sqlite3.Error:
        conn.execute("INSERT OR REPLACE INTO meta(key, value) VALUES ('fts5', '0')")

    conn.execute(
        "INSERT OR REPLACE INTO meta(key, value) VALUES ('schema_version', ?)",
        (str(SCHEMA_VERSION),),
    )
    conn.commit()


def split_chunks(text: str, max_chars: int = 1800) -> list[str]:
    paragraphs = [p.strip() for p in re.split(r"\n\s*\n", text) if p.strip()]
    if not paragraphs:
        return []

    chunks: list[str] = []
    current = ""
    for paragraph in paragraphs:
        if current and len(current) + len(paragraph) + 2 > max_chars:
            chunks.append(current)
            current = paragraph
        elif current:
            current += "\n\n" + paragraph
        else:
            current = paragraph
    if current:
        chunks.append(current)
    return chunks


def insert_chunk(
    conn: sqlite3.Connection,
    *,
    case_id: int,
    entry_id: int | None,
    domain: str,
    title_id: str,
    chunk_type: str,
    source_ref: str,
    text: str,
    created_at: str,
) -> None:
    embedding = json.dumps(embed_text(text), sort_keys=True, separators=(",", ":"))
    cur = conn.execute(
        """
        INSERT INTO chunks(case_id, entry_id, domain, title_id, chunk_type, source_ref, text, embedding_json, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (case_id, entry_id, domain, title_id, chunk_type, source_ref, text, embedding, created_at),
    )
    chunk_id = cur.lastrowid
    try:
        conn.execute(
            "INSERT INTO chunks_fts(rowid, text, source_ref) VALUES (?, ?, ?)",
            (chunk_id, text, source_ref),
        )
    except sqlite3.Error:
        pass


def resolve_case(conn: sqlite3.Connection, case_ref: str) -> sqlite3.Row:
    if case_ref.isdigit():
        row = conn.execute("SELECT * FROM cases WHERE id = ?", (int(case_ref),)).fetchone()
    else:
        row = conn.execute("SELECT * FROM cases WHERE slug = ? ORDER BY updated_at DESC LIMIT 1", (case_ref,)).fetchone()
    if row is None:
        raise SystemExit(f"Case not found: {case_ref}")
    return row


def upsert_case(args: argparse.Namespace) -> None:
    conn = connect(args.db)
    init_db(conn)
    now = utc_now()
    slug = args.slug or slugify(args.summary)
    title_id = args.title_id.upper() if args.title_id else ""
    existing = conn.execute(
        "SELECT * FROM cases WHERE domain = ? AND title_id = ? AND slug = ?",
        (args.domain, title_id, slug),
    ).fetchone()

    if existing:
        conn.execute(
            """
            UPDATE cases
            SET status = ?, severity = ?, platform_scope = ?, summary = ?,
                current_hypothesis = ?, updated_at = ?
            WHERE id = ?
            """,
            (
                args.status,
                args.severity,
                args.platform_scope,
                args.summary,
                args.hypothesis or "",
                now,
                existing["id"],
            ),
        )
        case_id = existing["id"]
        action = "updated"
    else:
        cur = conn.execute(
            """
            INSERT INTO cases(domain, title_id, slug, status, severity, platform_scope, summary, current_hypothesis, created_at, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                args.domain,
                title_id,
                slug,
                args.status,
                args.severity,
                args.platform_scope,
                args.summary,
                args.hypothesis or "",
                now,
                now,
            ),
        )
        case_id = cur.lastrowid
        action = "created"

    case_text = "\n".join(
        part
        for part in [
            f"Case {slug}",
            f"Domain: {args.domain}",
            f"Title ID: {title_id}",
            f"Status: {args.status}",
            f"Platform scope: {args.platform_scope}",
            f"Summary: {args.summary}",
            f"Current hypothesis: {args.hypothesis or ''}",
        ]
        if part
    )
    insert_chunk(
        conn,
        case_id=case_id,
        entry_id=None,
        domain=args.domain,
        title_id=title_id,
        chunk_type="case",
        source_ref=f"case:{slug}",
        text=case_text,
        created_at=now,
    )
    conn.commit()
    print(f"{action} case {case_id}: {slug}")


def add_entry(args: argparse.Namespace) -> None:
    conn = connect(args.db)
    init_db(conn)
    case = resolve_case(conn, args.case)
    now = utc_now()
    artifacts = [item for item in args.artifact if item]
    shader_hashes = [item.lower() for item in args.shader_hash if item]
    cur = conn.execute(
        """
        INSERT INTO entries(case_id, entry_type, platform, summary, body, artifact_refs_json,
                            shader_hashes_json, commit_hash, source, recency_pin, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            case["id"],
            args.type,
            args.platform,
            args.summary,
            args.body or "",
            json.dumps(artifacts),
            json.dumps(shader_hashes),
            args.commit or "",
            args.source,
            1 if args.pin else 0,
            now,
        ),
    )
    entry_id = cur.lastrowid
    conn.execute("UPDATE cases SET updated_at = ? WHERE id = ?", (now, case["id"]))

    text = "\n".join(
        part
        for part in [
            f"{args.type.title()} for {case['slug']}",
            f"Platform: {args.platform}",
            f"Summary: {args.summary}",
            f"Shader hashes: {', '.join(shader_hashes)}" if shader_hashes else "",
            f"Artifacts: {', '.join(artifacts)}" if artifacts else "",
            args.body or "",
        ]
        if part
    )
    for index, chunk in enumerate(split_chunks(text) or [text]):
        insert_chunk(
            conn,
            case_id=case["id"],
            entry_id=entry_id,
            domain=case["domain"],
            title_id=case["title_id"] or "",
            chunk_type=args.type,
            source_ref=f"entry:{entry_id}:{index}",
            text=chunk,
            created_at=now,
        )
    conn.commit()
    print(f"added {args.type} entry {entry_id} to case {case['id']}: {case['slug']}")


def list_cases(args: argparse.Namespace) -> None:
    conn = connect(args.db)
    init_db(conn)
    where = []
    params: list[Any] = []
    if args.status:
        where.append("status = ?")
        params.append(args.status)
    if args.domain:
        where.append("domain = ?")
        params.append(args.domain)
    if args.title_id:
        where.append("title_id = ?")
        params.append(args.title_id.upper())
    sql = "SELECT * FROM cases"
    if where:
        sql += " WHERE " + " AND ".join(where)
    sql += " ORDER BY updated_at DESC"
    for row in conn.execute(sql, params).fetchall():
        print(f"{row['id']:>3} {row['updated_at']} {row['domain']:<8} {row['title_id'] or '-':<10} {row['status']:<10} {row['slug']} - {row['summary']}")


def show_case(args: argparse.Namespace) -> None:
    conn = connect(args.db)
    init_db(conn)
    case = resolve_case(conn, args.case)
    print(f"Case {case['id']}: {case['slug']}")
    print(f"Domain: {case['domain']}")
    print(f"Title ID: {case['title_id'] or '-'}")
    print(f"Status: {case['status']}")
    print(f"Severity: {case['severity']}")
    print(f"Platform scope: {case['platform_scope']}")
    print(f"Updated: {case['updated_at']}")
    print(f"Summary: {case['summary']}")
    if case["current_hypothesis"]:
        print(f"Hypothesis: {case['current_hypothesis']}")
    print("")
    rows = conn.execute(
        "SELECT * FROM entries WHERE case_id = ? ORDER BY created_at DESC LIMIT ?",
        (case["id"], args.limit),
    ).fetchall()
    for row in rows:
        print(f"[{row['created_at']}] entry {row['id']} {row['entry_type']} {row['platform']}: {row['summary']}")
        if row["shader_hashes_json"] != "[]":
            print(f"  shader hashes: {', '.join(json.loads(row['shader_hashes_json']))}")
        if row["artifact_refs_json"] != "[]":
            print(f"  artifacts: {', '.join(json.loads(row['artifact_refs_json']))}")
        if row["body"]:
            body = row["body"].replace("\n", " ")
            print(f"  {body[:400]}{'...' if len(body) > 400 else ''}")


def keyword_score(query_tokens: set[str], text: str) -> float:
    if not query_tokens:
        return 0.0
    tokens = set(tokenize(text))
    return len(query_tokens & tokens) / max(1, len(query_tokens))


def recency_score(created_at: str) -> float:
    try:
        age = datetime.now(timezone.utc) - parse_time(created_at)
    except ValueError:
        return 0.0
    if age <= timedelta(days=7):
        return 1.0
    if age <= timedelta(days=30):
        return 0.7
    if age <= timedelta(days=180):
        return 0.35
    return 0.0


def search(args: argparse.Namespace) -> None:
    conn = connect(args.db)
    init_db(conn)
    query_embedding = embed_text(args.query)
    query_tokens = set(tokenize(args.query))
    where = []
    params: list[Any] = []
    if args.domain:
        where.append("domain = ?")
        params.append(args.domain)
    if args.title_id:
        where.append("title_id = ?")
        params.append(args.title_id.upper())
    if args.recent_days and not args.long_term:
        cutoff = (datetime.now(timezone.utc) - timedelta(days=args.recent_days)).replace(microsecond=0).isoformat()
        where.append("created_at >= ?")
        params.append(cutoff)

    sql = "SELECT * FROM chunks"
    if where:
        sql += " WHERE " + " AND ".join(where)
    sql += " ORDER BY created_at DESC LIMIT 1000"
    rows = conn.execute(sql, params).fetchall()

    scored = []
    for row in rows:
        embedding = json.loads(row["embedding_json"] or "{}")
        vector = cosine_sparse(query_embedding, embedding)
        keyword = keyword_score(query_tokens, row["text"])
        recent = 0.0 if args.long_term else recency_score(row["created_at"])
        score = (0.68 * vector) + (0.22 * keyword) + (0.10 * recent)
        if score > 0 or not query_tokens:
            scored.append((score, vector, keyword, recent, row))

    scored.sort(key=lambda item: item[0], reverse=True)
    top = scored[: args.limit]
    if not top:
        scope = "long-term" if args.long_term else f"last {args.recent_days} days"
        print(f"No matches in {scope}. Use --long-term only for recurring/architectural checks.")
        return

    for score, vector, keyword, recent, row in top:
        case = conn.execute("SELECT * FROM cases WHERE id = ?", (row["case_id"],)).fetchone()
        title = row["title_id"] or "-"
        print(
            f"[{score:.3f}] case={case['slug']} id={row['case_id']} domain={row['domain']} title={title} "
            f"type={row['chunk_type']} date={row['created_at']} vector={vector:.3f} keyword={keyword:.3f} recent={recent:.3f}"
        )
        text = re.sub(r"\s+", " ", row["text"]).strip()
        print(f"  {text[: args.preview]}{'...' if len(text) > args.preview else ''}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Vita3K Thor SQLite debug knowledge base")
    parser.add_argument("--db", type=Path, default=DEFAULT_DB, help=f"SQLite DB path (default: {DEFAULT_DB})")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("init", help="Create or migrate the debug knowledge DB")

    case_parser = sub.add_parser("case", help="Create/update/list/show cases")
    case_sub = case_parser.add_subparsers(dest="case_command", required=True)
    upsert = case_sub.add_parser("upsert", help="Create or update a case")
    upsert.add_argument("--domain", choices=["emulator", "game"], required=True)
    upsert.add_argument("--title-id", default="")
    upsert.add_argument("--slug", default="")
    upsert.add_argument("--status", default="active")
    upsert.add_argument("--severity", default="normal")
    upsert.add_argument("--platform-scope", default="windows-android")
    upsert.add_argument("--summary", required=True)
    upsert.add_argument("--hypothesis", default="")
    upsert.set_defaults(func=upsert_case)

    list_cmd = case_sub.add_parser("list", help="List cases")
    list_cmd.add_argument("--status", default="")
    list_cmd.add_argument("--domain", choices=["emulator", "game"], default="")
    list_cmd.add_argument("--title-id", default="")
    list_cmd.set_defaults(func=list_cases)

    show = case_sub.add_parser("show", help="Show one case and recent entries")
    show.add_argument("case")
    show.add_argument("--limit", type=int, default=20)
    show.set_defaults(func=show_case)

    entry = sub.add_parser("entry", help="Add a case entry")
    entry_sub = entry.add_subparsers(dest="entry_command", required=True)
    add = entry_sub.add_parser("add", help="Add an observation/decision/fix/test entry")
    add.add_argument("--case", required=True)
    add.add_argument("--type", choices=["observation", "decision", "fix", "regression", "question", "repro", "build", "test", "note"], required=True)
    add.add_argument("--platform", default="unknown")
    add.add_argument("--summary", required=True)
    add.add_argument("--body", default="")
    add.add_argument("--artifact", action="append", default=[])
    add.add_argument("--shader-hash", action="append", default=[])
    add.add_argument("--commit", default="")
    add.add_argument("--source", default="codex")
    add.add_argument("--pin", action="store_true")
    add.set_defaults(func=add_entry)

    search_cmd = sub.add_parser("search", help="Search by recency, text, and local hashed embeddings")
    search_cmd.add_argument("query")
    search_cmd.add_argument("--domain", choices=["emulator", "game"], default="")
    search_cmd.add_argument("--title-id", default="")
    search_cmd.add_argument("--recent-days", type=int, default=30)
    search_cmd.add_argument("--long-term", action="store_true", help="Search all history instead of recent-first")
    search_cmd.add_argument("--limit", type=int, default=8)
    search_cmd.add_argument("--preview", type=int, default=360)
    search_cmd.set_defaults(func=search)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.command == "init":
        conn = connect(args.db)
        init_db(conn)
        print(f"initialized {args.db}")
        return 0
    if hasattr(args, "func"):
        args.func(args)
        return 0
    parser.print_help()
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
