<!-- managed-by: jam-coding-role -->
# Explicit artifact and cloud Pro handoff

Artifact handoff is an optional stage-delivery capability. Run it only when the Owner requests a bundle/cloud review or a named scientific stage explicitly defines artifact delivery. It is not part of ordinary task closure.

## Cloud Pro handoff order

An Owner request to send the current stage to a cloud Pro reviewer authorizes the **in-scope handoff commit and push** unless the Owner explicitly says otherwise. Main must still exclude unrelated changes and respect branch protection.

Before generating the cloud prompt:

1. inspect the Git diff and commit only the stage's Git-visible source/config/document changes;
2. push the configured current/review branch;
3. verify the remote-tracking branch resolves to the same commit as local `HEAD`;
4. record repository URL、branch and full commit SHA;
5. pack and upload the selected Worker artifacts;
6. generate `PRO_REVIEW_PROMPT.md` naming the exact Drive task folder and Worker ZIP files;
7. after review, the cloud Pro uploads its full-answer ZIP into that **same task folder**.

If commit/push is not authorized, branch policy blocks it, or the remote commit cannot be verified, stop and report the handoff as blocked. Do not give the cloud reviewer a prompt that claims access to unpublished code.

## Selection boundary

Use a positive allowlist over relevant untracked/ignored outputs. Scan sensitive names and small text content, reject symlinks, enforce file/bundle limits, and require explicit checkpoint opt-in. Never package the entire untracked tree.

## Compressed-ZIP cloud limit and semantic splitting

The default limit applies to the **final compressed size of each generated `.zip` file**, not to the raw source files inside it. Every cloud-facing ZIP must be at most **95 MiB (99,614,720 bytes)**.

When a generated ZIP exceeds the compressed-size limit:

1. create ordinary, independently readable ZIP archives grouped by meaning;
2. split a still-oversized semantic group into further ordinary ZIP files;
3. never create `.z01/.z02/.zip`、`.zip.001` or similar reconstruction-dependent volumes;
4. never binary-slice a checkpoint; exclude nonessential models or use a separately authorized authenticated rclone exception.

## One task folder, two delivery roles

Use one immutable task folder:

```text
Pro_Space/<project>/<worktree>/<stage>/<timestamp>__<git-short-sha>/
```

Worker artifacts and the Pro answer stay in this same folder. Distinguish them by filename prefix, not by creating a second task folder:

```text
worker_delivery__BUNDLE_INDEX.md
worker_delivery__BUNDLE_MANIFEST.json
worker_delivery__PRO_HANDOFF.md
worker_delivery__source_and_configs.zip
worker_delivery__logs_and_metrics.zip
worker_delivery__plots_and_evidence.zip
worker_delivery__checkpoints_part01.zip
pro_delivery__full_review.zip
```

New handoffs use these prefixes. Historical releases do not need renaming. Create new releases only; do not overwrite, move or delete previous releases.

## Cloud Pro output contract

The cloud Pro gives two synchronized outputs.

### A. Concise Owner response in the conversation

Use this order:

1. any requested preliminary answer;
2. high-value insights and findings;
3. the requested diagnosis、stage acceptance、fact-check or QA result;
4. optional research novelty or overlooked algorithm、engineering or data contribution;
5. the exact Drive task folder、`pro_delivery__full_review.zip` address and a copy-ready prompt for the local Worker AI.

This response is intentionally concise so the Owner can review it quickly.

### B. Full local-Worker delivery

Upload `pro_delivery__full_review.zip` to the same task folder. It is a normal standard ZIP, at most 95 MiB compressed, containing:

- `FULL_REVIEW.md`: detailed insights/findings; detailed diagnosis/acceptance/fact-check/QA with evidence、inference、unknowns and local-only checks separated; optional novelty section;
- `LOCAL_WORKER_PARSE_PROMPT.md`: exactly the same copy-ready Worker prompt shown in concise item 5.

If the Pro runtime cannot upload to Drive, it must report `NOT_UPLOADED`, provide the two files and ZIP for download, and must not invent a Drive URL.

## Role boundary

The cloud Pro must think independently and research cautiously, but cannot inspect unbundled local logs、resolved runtime、IsaacLab/GPU/process state or hardware. Cloud assumptions must not become over-strict local scientific gates or release blockers. The local AI retains reasonable authority over production feasibility、commands、resources and admission thresholds.

## Authorization and upload

Packing and upload require an explicit Owner request or named stage-closure trigger. The public-write Drive permission is standing authorization for create-only stage artifacts, not authorization to upload arbitrary files, credentials or an unscreened worktree.

Choose upload capability in this order: connected Google Drive upload action; reliable browser/computer-use; authenticated rclone; otherwise produce the release locally and report `NOT_UPLOADED`.

A public editor link does not become anonymous Drive API credentials. Record an upload receipt only after the destination is verified.
