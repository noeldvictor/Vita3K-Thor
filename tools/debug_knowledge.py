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
SCHEMA_VERSION = 3
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

        CREATE TABLE IF NOT EXISTS attempts (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            case_id INTEGER NOT NULL REFERENCES cases(id) ON DELETE CASCADE,
            fingerprint TEXT NOT NULL,
            status TEXT NOT NULL CHECK (status IN (
                'planned', 'running', 'succeeded', 'failed',
                'inconclusive', 'superseded'
            )),
            platform TEXT NOT NULL DEFAULT 'unknown',
            subsystem TEXT NOT NULL DEFAULT '',
            hypothesis TEXT NOT NULL,
            change_summary TEXT NOT NULL DEFAULT '',
            test_command TEXT NOT NULL DEFAULT '',
            expected_signal TEXT NOT NULL DEFAULT '',
            result_summary TEXT NOT NULL DEFAULT '',
            artifact_refs_json TEXT NOT NULL DEFAULT '[]',
            shader_hashes_json TEXT NOT NULL DEFAULT '[]',
            commit_hash TEXT NOT NULL DEFAULT '',
            supersedes_attempt_id INTEGER REFERENCES attempts(id),
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            UNIQUE(case_id, fingerprint)
        );

        CREATE TABLE IF NOT EXISTS compat_checks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            case_id INTEGER REFERENCES cases(id) ON DELETE SET NULL,
            domain TEXT NOT NULL DEFAULT 'game' CHECK (domain IN ('emulator', 'game')),
            title_id TEXT NOT NULL DEFAULT '',
            title_name TEXT NOT NULL DEFAULT '',
            platform TEXT NOT NULL DEFAULT 'unknown',
            commit_hash TEXT NOT NULL,
            commit_label TEXT NOT NULL DEFAULT '',
            build_type TEXT NOT NULL DEFAULT '',
            status TEXT NOT NULL CHECK (status IN (
                'works', 'broken', 'regressed', 'partial',
                'blocked', 'unknown'
            )),
            scene TEXT NOT NULL DEFAULT '',
            summary TEXT NOT NULL,
            body TEXT NOT NULL DEFAULT '',
            artifact_refs_json TEXT NOT NULL DEFAULT '[]',
            tested_at TEXT NOT NULL,
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
        CREATE INDEX IF NOT EXISTS idx_attempts_case ON attempts(case_id);
        CREATE INDEX IF NOT EXISTS idx_attempts_status ON attempts(status);
        CREATE INDEX IF NOT EXISTS idx_attempts_fingerprint ON attempts(fingerprint);
        CREATE INDEX IF NOT EXISTS idx_compat_title ON compat_checks(title_id);
        CREATE INDEX IF NOT EXISTS idx_compat_commit ON compat_checks(commit_hash);
        CREATE INDEX IF NOT EXISTS idx_compat_platform ON compat_checks(platform);
        CREATE INDEX IF NOT EXISTS idx_compat_status ON compat_checks(status);
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


def delete_chunks_by_source_prefix(conn: sqlite3.Connection, source_prefix: str) -> None:
    rows = conn.execute("SELECT id FROM chunks WHERE source_ref LIKE ?", (f"{source_prefix}%",)).fetchall()
    for row in rows:
        try:
            conn.execute("DELETE FROM chunks_fts WHERE rowid = ?", (row["id"],))
        except sqlite3.Error:
            pass
    conn.execute("DELETE FROM chunks WHERE source_ref LIKE ?", (f"{source_prefix}%",))


def normalize_fingerprint_part(value: str) -> str:
    return re.sub(r"\s+", " ", value.strip().lower())


def make_attempt_fingerprint(case_slug: str, args: argparse.Namespace, shader_hashes: list[str]) -> str:
    if args.fingerprint:
        return slugify(args.fingerprint)

    parts = [
        case_slug,
        args.platform,
        args.subsystem,
        args.hypothesis,
        ",".join(sorted(shader_hashes)),
    ]
    normalized = "||".join(normalize_fingerprint_part(part) for part in parts)
    digest = hashlib.sha256(normalized.encode("utf-8")).hexdigest()[:12]
    prefix = slugify(args.subsystem or args.platform or "attempt")[:32]
    return f"{prefix}-{digest}"


def attempt_text(case_slug: str, attempt: dict[str, Any]) -> str:
    shader_hashes = attempt.get("shader_hashes", [])
    artifacts = attempt.get("artifacts", [])
    parts = [
        f"Attempt {attempt['fingerprint']} for {case_slug}",
        f"Status: {attempt['status']}",
        f"Platform: {attempt['platform']}",
        f"Subsystem: {attempt['subsystem']}" if attempt.get("subsystem") else "",
        f"Hypothesis: {attempt['hypothesis']}",
        f"Change: {attempt['change_summary']}" if attempt.get("change_summary") else "",
        f"Test command: {attempt['test_command']}" if attempt.get("test_command") else "",
        f"Expected signal: {attempt['expected_signal']}" if attempt.get("expected_signal") else "",
        f"Result: {attempt['result_summary']}" if attempt.get("result_summary") else "",
        f"Shader hashes: {', '.join(shader_hashes)}" if shader_hashes else "",
        f"Artifacts: {', '.join(artifacts)}" if artifacts else "",
        f"Commit: {attempt['commit_hash']}" if attempt.get("commit_hash") else "",
        f"Supersedes attempt: {attempt['supersedes_attempt_id']}" if attempt.get("supersedes_attempt_id") else "",
    ]
    return "\n".join(part for part in parts if part)


def compat_text(row: dict[str, Any], case_slug: str = "") -> str:
    artifacts = row.get("artifacts", [])
    parts = [
        "Compatibility checkpoint",
        f"Case: {case_slug}" if case_slug else "",
        f"Domain: {row.get('domain', 'game')}",
        f"Title ID: {row.get('title_id', '') or '-'}",
        f"Title name: {row.get('title_name', '')}" if row.get("title_name") else "",
        f"Platform: {row['platform']}",
        f"Commit: {row['commit_hash']}",
        f"Commit label: {row.get('commit_label', '')}" if row.get("commit_label") else "",
        f"Build type: {row.get('build_type', '')}" if row.get("build_type") else "",
        f"Status: {row['status']}",
        f"Scene: {row.get('scene', '')}" if row.get("scene") else "",
        f"Summary: {row['summary']}",
        f"Artifacts: {', '.join(artifacts)}" if artifacts else "",
        f"Tested at: {row['tested_at']}",
        row.get("body", ""),
    ]
    return "\n".join(part for part in parts if part)


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


def add_attempt(args: argparse.Namespace) -> None:
    conn = connect(args.db)
    init_db(conn)
    case = resolve_case(conn, args.case)
    now = utc_now()
    artifacts = [item for item in args.artifact if item]
    shader_hashes = [item.lower() for item in args.shader_hash if item]
    fingerprint = make_attempt_fingerprint(case["slug"], args, shader_hashes)
    supersedes_attempt_id = args.supersedes if args.supersedes else None

    existing = conn.execute(
        "SELECT * FROM attempts WHERE case_id = ? AND fingerprint = ?",
        (case["id"], fingerprint),
    ).fetchone()

    if existing:
        conn.execute(
            """
            UPDATE attempts
            SET status = ?, platform = ?, subsystem = ?, hypothesis = ?,
                change_summary = ?, test_command = ?, expected_signal = ?,
                result_summary = ?, artifact_refs_json = ?,
                shader_hashes_json = ?, commit_hash = ?,
                supersedes_attempt_id = ?, updated_at = ?
            WHERE id = ?
            """,
            (
                args.status,
                args.platform,
                args.subsystem or "",
                args.hypothesis,
                args.change or "",
                args.test_command or "",
                args.expected or "",
                args.result or "",
                json.dumps(artifacts),
                json.dumps(shader_hashes),
                args.commit or "",
                supersedes_attempt_id,
                now,
                existing["id"],
            ),
        )
        attempt_id = existing["id"]
        action = "updated"
        delete_chunks_by_source_prefix(conn, f"attempt:{attempt_id}:")
    else:
        cur = conn.execute(
            """
            INSERT INTO attempts(case_id, fingerprint, status, platform, subsystem,
                                 hypothesis, change_summary, test_command,
                                 expected_signal, result_summary, artifact_refs_json,
                                 shader_hashes_json, commit_hash, supersedes_attempt_id,
                                 created_at, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                case["id"],
                fingerprint,
                args.status,
                args.platform,
                args.subsystem or "",
                args.hypothesis,
                args.change or "",
                args.test_command or "",
                args.expected or "",
                args.result or "",
                json.dumps(artifacts),
                json.dumps(shader_hashes),
                args.commit or "",
                supersedes_attempt_id,
                now,
                now,
            ),
        )
        attempt_id = cur.lastrowid
        action = "added"

    conn.execute("UPDATE cases SET updated_at = ? WHERE id = ?", (now, case["id"]))
    text = attempt_text(
        case["slug"],
        {
            "fingerprint": fingerprint,
            "status": args.status,
            "platform": args.platform,
            "subsystem": args.subsystem or "",
            "hypothesis": args.hypothesis,
            "change_summary": args.change or "",
            "test_command": args.test_command or "",
            "expected_signal": args.expected or "",
            "result_summary": args.result or "",
            "artifacts": artifacts,
            "shader_hashes": shader_hashes,
            "commit_hash": args.commit or "",
            "supersedes_attempt_id": supersedes_attempt_id,
        },
    )
    for index, chunk in enumerate(split_chunks(text) or [text]):
        insert_chunk(
            conn,
            case_id=case["id"],
            entry_id=None,
            domain=case["domain"],
            title_id=case["title_id"] or "",
            chunk_type="attempt",
            source_ref=f"attempt:{attempt_id}:{index}",
            text=chunk,
            created_at=now,
        )
    conn.commit()
    print(f"{action} attempt {attempt_id} for case {case['id']}: {case['slug']}")
    print(f"fingerprint: {fingerprint}")


def add_compat(args: argparse.Namespace) -> None:
    conn = connect(args.db)
    init_db(conn)
    now = utc_now()
    title_id = args.title_id.upper() if args.title_id else ""
    artifacts = [item for item in args.artifact if item]
    case = resolve_case(conn, args.case) if args.case else None
    tested_at = args.tested_at or now

    cur = conn.execute(
        """
        INSERT INTO compat_checks(case_id, domain, title_id, title_name, platform,
                                  commit_hash, commit_label, build_type, status,
                                  scene, summary, body, artifact_refs_json,
                                  tested_at, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            case["id"] if case else None,
            args.domain,
            title_id,
            args.title_name or "",
            args.platform,
            args.commit,
            args.commit_label or "",
            args.build_type or "",
            args.status,
            args.scene or "",
            args.summary,
            args.body or "",
            json.dumps(artifacts),
            tested_at,
            now,
        ),
    )
    compat_id = cur.lastrowid

    if case:
        conn.execute("UPDATE cases SET updated_at = ? WHERE id = ?", (now, case["id"]))
        chunk_case = case
    else:
        slug = slugify(f"compatibility {title_id or args.title_name or args.platform}")
        existing = conn.execute(
            "SELECT * FROM cases WHERE domain = ? AND title_id = ? AND slug = ?",
            (args.domain, title_id, slug),
        ).fetchone()
        if existing:
            chunk_case = existing
            conn.execute("UPDATE cases SET updated_at = ? WHERE id = ?", (now, existing["id"]))
        else:
            summary = f"Compatibility checkpoints for {title_id or args.title_name or args.platform}"
            new_case = conn.execute(
                """
                INSERT INTO cases(domain, title_id, slug, status, severity, platform_scope,
                                  summary, current_hypothesis, created_at, updated_at)
                VALUES (?, ?, ?, 'active', 'normal', ?, ?, '', ?, ?)
                """,
                (args.domain, title_id, slug, args.platform, summary, now, now),
            )
            chunk_case = conn.execute("SELECT * FROM cases WHERE id = ?", (new_case.lastrowid,)).fetchone()
            conn.execute("UPDATE compat_checks SET case_id = ? WHERE id = ?", (chunk_case["id"], compat_id))

    text = compat_text(
        {
            "domain": args.domain,
            "title_id": title_id,
            "title_name": args.title_name or "",
            "platform": args.platform,
            "commit_hash": args.commit,
            "commit_label": args.commit_label or "",
            "build_type": args.build_type or "",
            "status": args.status,
            "scene": args.scene or "",
            "summary": args.summary,
            "body": args.body or "",
            "artifacts": artifacts,
            "tested_at": tested_at,
        },
        chunk_case["slug"] if chunk_case else "",
    )
    for index, chunk in enumerate(split_chunks(text) or [text]):
        insert_chunk(
            conn,
            case_id=chunk_case["id"],
            entry_id=None,
            domain=args.domain,
            title_id=title_id,
            chunk_type="compat",
            source_ref=f"compat:{compat_id}:{index}",
            text=chunk,
            created_at=now,
        )
    conn.commit()
    print(f"added compatibility checkpoint {compat_id}: {title_id or args.title_name or '-'} {args.platform} {args.status} at {args.commit}")


def list_compat(args: argparse.Namespace) -> None:
    conn = connect(args.db)
    init_db(conn)
    where = []
    params: list[Any] = []
    if args.title_id:
        where.append("title_id = ?")
        params.append(args.title_id.upper())
    if args.platform:
        where.append("platform = ?")
        params.append(args.platform)
    if args.status:
        where.append("status = ?")
        params.append(args.status)
    if args.commit:
        where.append("commit_hash LIKE ?")
        params.append(f"{args.commit}%")
    if args.case:
        case = resolve_case(conn, args.case)
        where.append("case_id = ?")
        params.append(case["id"])

    sql = "SELECT * FROM compat_checks"
    if where:
        sql += " WHERE " + " AND ".join(where)
    sql += " ORDER BY tested_at DESC, created_at DESC LIMIT ?"
    params.append(args.limit)

    rows = conn.execute(sql, params).fetchall()
    for row in rows:
        case_slug = "-"
        if row["case_id"]:
            case = conn.execute("SELECT slug FROM cases WHERE id = ?", (row["case_id"],)).fetchone()
            if case:
                case_slug = case["slug"]
        artifacts = json.loads(row["artifact_refs_json"] or "[]")
        print(
            f"{row['id']:>3} {row['tested_at']} {row['status']:<9} {row['platform']:<12} "
            f"{row['title_id'] or '-':<10} {row['commit_hash'][:12]:<12} case={case_slug}"
        )
        print(f"    {row['summary'][:180]}{'...' if len(row['summary']) > 180 else ''}")
        if row["scene"]:
            print(f"    scene: {row['scene'][:180]}{'...' if len(row['scene']) > 180 else ''}")
        if artifacts:
            print(f"    artifacts: {', '.join(artifacts)}")


def list_attempts(args: argparse.Namespace) -> None:
    conn = connect(args.db)
    init_db(conn)
    where = []
    params: list[Any] = []
    if args.case:
        case = resolve_case(conn, args.case)
        where.append("case_id = ?")
        params.append(case["id"])
    if args.status:
        where.append("status = ?")
        params.append(args.status)
    if args.platform:
        where.append("platform = ?")
        params.append(args.platform)
    if args.subsystem:
        where.append("subsystem = ?")
        params.append(args.subsystem)

    sql = "SELECT * FROM attempts"
    if where:
        sql += " WHERE " + " AND ".join(where)
    sql += " ORDER BY updated_at DESC LIMIT ?"
    params.append(args.limit)

    for row in conn.execute(sql, params).fetchall():
        case = conn.execute("SELECT * FROM cases WHERE id = ?", (row["case_id"],)).fetchone()
        print(
            f"{row['id']:>3} {row['updated_at']} {row['status']:<12} {row['platform']:<8} "
            f"{row['subsystem'] or '-':<18} {row['fingerprint']} case={case['slug']}"
        )
        print(f"    hypothesis: {row['hypothesis'][:180]}{'...' if len(row['hypothesis']) > 180 else ''}")
        if row["result_summary"]:
            print(f"    result: {row['result_summary'][:180]}{'...' if len(row['result_summary']) > 180 else ''}")


def check_attempt(args: argparse.Namespace) -> None:
    conn = connect(args.db)
    init_db(conn)
    case = resolve_case(conn, args.case)
    shader_hashes = [item.lower() for item in args.shader_hash if item]
    fingerprint = make_attempt_fingerprint(case["slug"], args, shader_hashes)

    exact = conn.execute(
        "SELECT * FROM attempts WHERE case_id = ? AND fingerprint = ?",
        (case["id"], fingerprint),
    ).fetchone()
    if exact:
        print(
            f"EXACT attempt exists: id={exact['id']} status={exact['status']} "
            f"updated={exact['updated_at']} fingerprint={exact['fingerprint']}"
        )
        print(f"  hypothesis: {exact['hypothesis']}")
        if exact["change_summary"]:
            print(f"  change: {exact['change_summary']}")
        if exact["result_summary"]:
            print(f"  result: {exact['result_summary']}")
        if exact["artifact_refs_json"] != "[]":
            print(f"  artifacts: {', '.join(json.loads(exact['artifact_refs_json']))}")
        print("")
    else:
        print(f"No exact attempt fingerprint yet: {fingerprint}")
        print("")

    query_text = attempt_text(
        case["slug"],
        {
            "fingerprint": fingerprint,
            "status": "planned",
            "platform": args.platform,
            "subsystem": args.subsystem or "",
            "hypothesis": args.hypothesis,
            "change_summary": args.change or "",
            "test_command": args.test_command or "",
            "expected_signal": args.expected or "",
            "result_summary": "",
            "artifacts": [],
            "shader_hashes": shader_hashes,
            "commit_hash": "",
            "supersedes_attempt_id": None,
        },
    )
    query_embedding = embed_text(query_text)
    query_tokens = set(tokenize(query_text))
    rows = conn.execute("SELECT * FROM attempts WHERE case_id = ? ORDER BY updated_at DESC LIMIT 500", (case["id"],)).fetchall()

    scored = []
    for row in rows:
        row_text = attempt_text(
            case["slug"],
            {
                "fingerprint": row["fingerprint"],
                "status": row["status"],
                "platform": row["platform"],
                "subsystem": row["subsystem"],
                "hypothesis": row["hypothesis"],
                "change_summary": row["change_summary"],
                "test_command": row["test_command"],
                "expected_signal": row["expected_signal"],
                "result_summary": row["result_summary"],
                "artifacts": json.loads(row["artifact_refs_json"] or "[]"),
                "shader_hashes": json.loads(row["shader_hashes_json"] or "[]"),
                "commit_hash": row["commit_hash"],
                "supersedes_attempt_id": row["supersedes_attempt_id"],
            },
        )
        vector = cosine_sparse(query_embedding, embed_text(row_text))
        keyword = keyword_score(query_tokens, row_text)
        status_penalty = 0.08 if row["status"] in {"failed", "inconclusive", "superseded"} else 0.0
        score = (0.66 * vector) + (0.34 * keyword) + status_penalty
        if score > 0:
            scored.append((score, vector, keyword, row))

    scored.sort(key=lambda item: item[0], reverse=True)
    top = scored[: args.limit]
    if not top:
        print("No similar attempts in this case.")
        return

    print("Similar attempts:")
    for score, vector, keyword, row in top:
        print(
            f"[{score:.3f}] id={row['id']} status={row['status']} platform={row['platform']} "
            f"subsystem={row['subsystem'] or '-'} updated={row['updated_at']} fingerprint={row['fingerprint']} "
            f"vector={vector:.3f} keyword={keyword:.3f}"
        )
        print(f"  hypothesis: {row['hypothesis'][: args.preview]}{'...' if len(row['hypothesis']) > args.preview else ''}")
        if row["change_summary"]:
            print(f"  change: {row['change_summary'][: args.preview]}{'...' if len(row['change_summary']) > args.preview else ''}")
        if row["result_summary"]:
            print(f"  result: {row['result_summary'][: args.preview]}{'...' if len(row['result_summary']) > args.preview else ''}")


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
    attempts = conn.execute(
        "SELECT * FROM attempts WHERE case_id = ? ORDER BY updated_at DESC LIMIT ?",
        (case["id"], min(args.limit, 10)),
    ).fetchall()
    if attempts:
        print("Recent attempts:")
        for row in attempts:
            print(
                f"[{row['updated_at']}] attempt {row['id']} {row['status']} "
                f"{row['platform']} {row['subsystem'] or '-'}: {row['fingerprint']}"
            )
            print(f"  hypothesis: {row['hypothesis'][:220]}{'...' if len(row['hypothesis']) > 220 else ''}")
            if row["result_summary"]:
                print(f"  result: {row['result_summary'][:220]}{'...' if len(row['result_summary']) > 220 else ''}")
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

    attempt = sub.add_parser("attempt", help="Track debug hypotheses/tests so failed loops are not repeated")
    attempt_sub = attempt.add_subparsers(dest="attempt_command", required=True)

    attempt_statuses = ["planned", "running", "succeeded", "failed", "inconclusive", "superseded"]
    attempt_add = attempt_sub.add_parser("add", help="Add or update a debug attempt by stable fingerprint")
    attempt_add.add_argument("--case", required=True)
    attempt_add.add_argument("--status", choices=attempt_statuses, required=True)
    attempt_add.add_argument("--platform", default="unknown")
    attempt_add.add_argument("--subsystem", default="")
    attempt_add.add_argument("--hypothesis", required=True)
    attempt_add.add_argument("--change", default="")
    attempt_add.add_argument("--test-command", default="")
    attempt_add.add_argument("--expected", default="")
    attempt_add.add_argument("--result", default="")
    attempt_add.add_argument("--artifact", action="append", default=[])
    attempt_add.add_argument("--shader-hash", action="append", default=[])
    attempt_add.add_argument("--commit", default="")
    attempt_add.add_argument("--fingerprint", default="", help="Optional manual fingerprint; otherwise derived from case/platform/subsystem/hypothesis/shader hashes")
    attempt_add.add_argument("--supersedes", type=int, default=0)
    attempt_add.set_defaults(func=add_attempt)

    attempt_check = attempt_sub.add_parser("check", help="Check whether a similar or exact attempt was already tried")
    attempt_check.add_argument("--case", required=True)
    attempt_check.add_argument("--platform", default="unknown")
    attempt_check.add_argument("--subsystem", default="")
    attempt_check.add_argument("--hypothesis", required=True)
    attempt_check.add_argument("--change", default="")
    attempt_check.add_argument("--test-command", default="")
    attempt_check.add_argument("--expected", default="")
    attempt_check.add_argument("--shader-hash", action="append", default=[])
    attempt_check.add_argument("--fingerprint", default="")
    attempt_check.add_argument("--limit", type=int, default=6)
    attempt_check.add_argument("--preview", type=int, default=220)
    attempt_check.set_defaults(func=check_attempt)

    attempt_list = attempt_sub.add_parser("list", help="List recent attempts")
    attempt_list.add_argument("--case", default="")
    attempt_list.add_argument("--status", choices=attempt_statuses, default="")
    attempt_list.add_argument("--platform", default="")
    attempt_list.add_argument("--subsystem", default="")
    attempt_list.add_argument("--limit", type=int, default=20)
    attempt_list.set_defaults(func=list_attempts)

    compat = sub.add_parser("compat", help="Track game/platform compatibility checkpoints by commit")
    compat_sub = compat.add_subparsers(dest="compat_command", required=True)
    compat_statuses = ["works", "broken", "regressed", "partial", "blocked", "unknown"]

    compat_add = compat_sub.add_parser("add", help="Add a compatibility checkpoint")
    compat_add.add_argument("--case", default="", help="Optional linked case slug/id")
    compat_add.add_argument("--domain", choices=["emulator", "game"], default="game")
    compat_add.add_argument("--title-id", default="")
    compat_add.add_argument("--title-name", default="")
    compat_add.add_argument("--platform", required=True)
    compat_add.add_argument("--commit", required=True)
    compat_add.add_argument("--commit-label", default="")
    compat_add.add_argument("--build-type", default="")
    compat_add.add_argument("--status", choices=compat_statuses, required=True)
    compat_add.add_argument("--scene", default="")
    compat_add.add_argument("--summary", required=True)
    compat_add.add_argument("--body", default="")
    compat_add.add_argument("--artifact", action="append", default=[])
    compat_add.add_argument("--tested-at", default="")
    compat_add.set_defaults(func=add_compat)

    compat_list = compat_sub.add_parser("list", help="List compatibility checkpoints")
    compat_list.add_argument("--case", default="")
    compat_list.add_argument("--title-id", default="")
    compat_list.add_argument("--platform", default="")
    compat_list.add_argument("--status", choices=compat_statuses, default="")
    compat_list.add_argument("--commit", default="")
    compat_list.add_argument("--limit", type=int, default=30)
    compat_list.set_defaults(func=list_compat)

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
