#!/usr/bin/env python3
"""Candidate inbox and index helper for optional durable-memory curation."""

from __future__ import annotations

import argparse
import json
import re
import uuid
from datetime import datetime, timezone
from pathlib import Path

INBOX = Path('.ai/runtime/memory-inbox')
INDEX = Path('memory/_index.json')


def now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec='seconds')


def candidate_path(candidate: str) -> Path:
    return INBOX / f'{candidate}.json'


def add(args: argparse.Namespace) -> int:
    INBOX.mkdir(parents=True, exist_ok=True)
    cid = f"mem-{datetime.now().strftime('%Y%m%d%H%M%S')}-{uuid.uuid4().hex[:8]}"
    data = {'id': cid, 'created_at': now(), 'title': args.title, 'scope': args.scope, 'status': 'candidate', 'source': args.source, 'evidence': args.evidence, 'suggested_action': args.action, 'target_route': args.target_route, 'body': args.body}
    path = candidate_path(cid)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(path)
    return 0


def list_candidates(_args: argparse.Namespace) -> int:
    if not INBOX.exists():
        print('NO CANDIDATES')
        return 0
    for path in sorted(INBOX.glob('*.json')):
        data = json.loads(path.read_text(encoding='utf-8'))
        print(f"{data['id']}\t{data.get('status')}\t{data.get('suggested_action')}\t{data.get('target_route')}\t{data.get('title')}")
    return 0


def show(args: argparse.Namespace) -> int:
    path = candidate_path(args.candidate)
    print(path.read_text(encoding='utf-8'))
    return 0


def mark(args: argparse.Namespace) -> int:
    path = candidate_path(args.candidate)
    data = json.loads(path.read_text(encoding='utf-8'))
    data['status'] = args.status
    data['resolved_at'] = now()
    data['resolution'] = args.resolution
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(path)
    return 0


def reindex(_args: argparse.Namespace) -> int:
    entries = []
    root = Path('memory')
    if root.exists():
        for path in sorted(root.rglob('description.md')):
            text = path.read_text(encoding='utf-8', errors='replace')
            status = 'active'; title = path.parent.name
            match = re.search(r'^status:\s*(.+)$', text, re.M)
            if match: status = match.group(1).strip()
            match = re.search(r'^name:\s*(.+)$', text, re.M)
            if match: title = match.group(1).strip()
            entries.append({'path': path.as_posix(), 'route': path.parent.as_posix(), 'title': title, 'status': status})
    INDEX.parent.mkdir(parents=True, exist_ok=True)
    INDEX.write_text(json.dumps({'generated_at': now(), 'entries': entries}, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(INDEX)
    return 0


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='cmd', required=True)
    a = sub.add_parser('candidate-add'); a.add_argument('--title', required=True); a.add_argument('--scope', default=''); a.add_argument('--source', default=''); a.add_argument('--evidence', default=''); a.add_argument('--action', choices=['create','append','move','split','merge','supersede','retire'], required=True); a.add_argument('--target-route', required=True); a.add_argument('--body', required=True); a.set_defaults(func=add)
    l = sub.add_parser('candidate-list'); l.set_defaults(func=list_candidates)
    s = sub.add_parser('candidate-show'); s.add_argument('--candidate', required=True); s.set_defaults(func=show)
    m = sub.add_parser('candidate-mark'); m.add_argument('--candidate', required=True); m.add_argument('--status', choices=['resolved','rejected','deferred'], required=True); m.add_argument('--resolution', required=True); m.set_defaults(func=mark)
    r = sub.add_parser('reindex'); r.set_defaults(func=reindex)
    return p


if __name__ == '__main__':
    args = parser().parse_args()
    raise SystemExit(args.func(args))
