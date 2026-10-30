# DataGuard ML Evaluation Protocol (Proposal)

**Status:** Draft for human review. Phase 1 only. No dataset, labels, recovery
implementation, model training, or evaluation results are created by this
document. Phase 2 must not begin until this protocol is explicitly approved.

## Evidence and decision labels used below

- **Inspected fact** means directly observed in the current repository at the
  time this draft was prepared.
- **Proposed decision** means a protocol rule for review, not an implemented
  or approved behavior.
- **Requires approval** identifies a decision that must be approved before the
  relevant later phase.
- **Unscorable limitation** means evidence is insufficient for a valid label;
  it must not be converted to a numeric zero.

## A. Purpose and scope

The objective is to evaluate whether DataGuard can estimate the measured outcome
of one specified, fixed, versioned file-recovery procedure on a defined population
of controlled FAT32 disk-image cases. Any prediction claim is limited to the
procedure version, feature schema, scenarios, and population represented by the
evaluation evidence. The model must not claim to predict every possible recovery
method, guarantee successful recovery, or prove that a file is permanently
unrecoverable.

This phase defines a protocol only. It does not implement recovery, wiping,
dataset generation, or model training. No files in `dataset/baseline/` or
existing ground-truth manifests are part of this draft's inspection or change
scope; they must remain immutable.

**Requires approval:** the prediction target, recovery procedure, feature
contract, split policy, metrics, and safety gates below.

## B. Prediction target

**Proposed target:**

`recovery_fraction = verified_original_bytes_recovered / original_file_size_bytes`

The unit is one uniquely identified original file in one controlled image
experiment, processed by one frozen recovery-procedure version.

Proposed measurement rules:

- `1.0`: every original logical byte position is verified, and the recovered
  output has the expected original logical size.
- A value strictly between `0.0` and `1.0`: the procedure produced an
  unambiguous mapping to original logical byte positions and verified a
  measurable subset of those positions.
- `0.0`: the fixed procedure completed successfully and returned no verified
  original byte positions. This describes that procedure's output only; it
  does not prove that another procedure cannot recover data.
- **Unscorable:** required original reference, identity, expected size,
  position mapping, comparison evidence, or procedure completion evidence is
  missing or ambiguous; the scenario is unsupported; or infrastructure
  failed.

Count each original logical byte position at most once. For overlapping output
ranges, take the union of uniquely verified original positions; do not sum
overlap. A byte counts only when the recovery procedure establishes its original logical
offset through independently documented provenance, and the byte value at that
offset matches the reference. The position mapping must not be inferred from
content matching alone: repeated or identical byte sequences can occur at multiple
logical positions. If the procedure cannot establish a unique original offset for
a byte, that byte is not verified; if the ambiguity prevents a defensible score,
mark the affected case unscorable. Record the mapping evidence used for scoring.

**Proposed zero-length policy:** zero-length originals are unscorable because
the denominator is zero. Do not substitute a convention such as score 1.0.

**Requires approval:** whether this target is scientifically appropriate and
whether byte-position provenance can be established by the chosen procedure.

## C. Recovery procedure

**Proposed starting point:** a single versioned, deterministic, read-only
reference procedure that attempts recovery from a deleted FAT32 directory
entry using validated metadata and a bounded cluster-chain procedure on a
regular disk-image file.

**Inspected facts:** `FileScanner` exposes a mockable `IReadOnlyVolumeReader`
with volume-relative bounded reads and identity revalidation. `scanFat32`
parses directory metadata and emits existing, deleted-candidate, invalid, and
ambiguous records. Deleted candidates are not operation-eligible. The
`DeletedEntryOperationTarget` defaults to unvalidated with operations
disabled. The mounted-volume reader is a Windows physical-volume access path;
it is not appropriate for this dataset workflow. `scanDeleted` currently
returns `Unsupported`; the metadata scanner is not a recovery implementation
and does not produce recovered file bytes.

The recovery behavior needed for this target is **not implemented or frozen**.
The target measures only the output of this fixed procedure; it is not a measure
of universal recoverability or the best result any alternative tool could achieve.
Before labeling, a separate approved tool specification must define:

1. **Candidate/reference match:** use a unique experiment identifier carried
   through a controlled manifest and image construction record. Do not match
   by filename alone. Require unambiguous filesystem identity and scenario
   association; otherwise mark unscorable.
2. **Expected size:** take the logical size from the immutable original-file
   reference manifest, independently bound to the file hash and experiment
   ID. A deleted directory-entry size is evidence to compare, not the
   authoritative expected size.
3. **FAT traversal:** validate BPB geometry, FAT bounds, cluster validity,
   cycles, duplicate references, and image boundaries with checked
   arithmetic before each bounded read. Enforce explicit byte, cluster, entry,
   depth, and time/cancellation limits. Any overflow, short read, malformed
   chain, stale identity, or unexpected image state fails closed.
4. **Fragmentation:** follow only a validated FAT chain when its identity and
   allocation history are unambiguous. Do not infer contiguous clusters from
   file size or directory metadata. Unsupported or ambiguous chains are
   unscorable.
5. **Reused clusters:** detect reuse from documented controlled scenario
   history and validated filesystem metadata. If old and replacement bytes
   cannot be distinguished or mapped to original positions, mark unscorable;
   do not guess.
6. **Partial recovery:** retain a deterministic mapping from recovered output
   ranges to original logical positions. Compare each mapped byte against the
   original reference and count the union of matching positions only.
7. **Versioning:** record procedure name, semantic version, source revision,
   configuration, and relevant parser/format versions with each label run.
   Any behavior-changing update creates a new procedure version and requires
   labels to be regenerated only from approved immutable source copies.

**Requires approval:** the detailed algorithm, limits, timeouts, handling of
FAT copies, deleted LFN metadata, and the definition of a verifiable output
position. No recovery code is authorized by this protocol.

## D. Ground-truth requirements

**Proposed immutable reference manifest fields:**

- Unique experiment ID and unique original-file ID.
- Original relative path/name for provenance, not as the sole identity.
- Original logical size and SHA-256 of exact original bytes.
- Scenario identifier and complete parameters, including deletion, reuse,
  overwrite, and fragmentation actions.
- Image identifier, image SHA-256, image format, and image-generation record.
- Generator name/version, configuration, and random seed where applicable.
- Recovery procedure name/version/source revision/configuration.
- Label value only for scored cases, scoring-tool version, and evidence
  references.
- Scoring status (`scored` or `unscorable`) and a required reason code/details
  for every unscorable case.

Reference bytes and approved manifests must be immutable after approval and
hash-verified before and after each experiment. A deterministic scoring tool
must derive labels; labels must not be hand-edited. The original image is a
read-only baseline; experiments use separate verified working copies. Recovery
outputs are stored outside source images.

**Inspected fact:** no dataset files are tracked by Git in the inspected
repository state. This does not establish the contents or existence of
untracked/ignored external artifacts. No verified original reference corpus,
reference hash manifest, or recovery labels were established in this phase.

## E. Pre-recovery feature contract

Features must be available to DataGuard before the fixed recovery procedure
runs, using only approved inputs. Original reference bytes, labels, recovered
output, and post-recovery findings are never prediction inputs.

### 1. Metadata potentially available before recovery

**Inspected facts:** `FileEntry` has status, known/unknown name and size flags,
size, directory-entry volume-relative offset, parent-directory cluster,
entry identity, volume identity, confidence, operation eligibility, and a
reason string. The FAT32 parser internally examines starting clusters, chain
validity, and reuse ambiguity, but `FileEntry` does not expose a dedicated
validated starting-cluster feature or structured allocation/reuse feature.
The selected drive metadata supplies filesystem type outside `FileEntry`.

**Proposed candidates, subject to availability and leakage review:** deleted
marker/status, size-known indicator and known directory-entry size, supported
filesystem, validated volume identity indicator, validated entry identity,
first-cluster validity only if exposed as structured validated metadata,
allocation/reuse evidence only if exposed as structured validated metadata,
and fragmentation hints only if supported by validated metadata. Do not parse
human-readable `reason` strings into model features.

### 2. Byte-content statistics

**Inspected facts:** `FeatureExtractor` accepts a caller-provided byte buffer
and returns ten numeric statistics: entropy, mean byte value, standard
deviation, zero-byte percentage, `0xFF` percentage, printable-byte percentage,
unique-byte ratio, dominant-byte percentage, byte range, and chi-square.
`IReadOnlyVolumeReader` offers bounded volume-relative reads, but the scanner
does not return candidate-specific content buffers or a validated mapping from
candidate bytes to original file positions. Thus the extractor's interface
does not establish safe, consistent candidate-level pre-recovery byte
availability.

These ten statistics are **not approved features** for this target until a
separate candidate-specific, pre-recovery byte source and mapping are designed,
reviewed, and proven not to depend on the original reference or recovery
output. Never invent candidate bytes, read guessed extents, or substitute
zero-filled buffers.

### 3. Prohibited/leaking features

Do not use original reference bytes or their content statistics, recovered
bytes, output hashes, recovery success/failure, target-derived metadata,
post-recovery allocation state, or any field created using the label/scoring
result. These are unavailable before recovery or leak the target.

### Missing-value rule

Represent missing/unknown values with a separately encoded missingness flag
and a schema-defined nonnumeric/masked representation, not numeric zero.
Reject rows if the implementation cannot preserve the distinction between
missing and a genuine zero. The exact encoding requires approval before
training.

## F. Dataset scenarios

**Proposed scenarios, using disposable regular FAT32 image files only:**

- Deleted file with no subsequent writes.
- Deleted file followed by controlled cluster reuse.
- Controlled overwrite of relevant clusters.
- Fragmented file, if construction and ground truth are reproducible.
- Invalid or ambiguous directory/FAT metadata.
- Unsupported, incomplete, truncated, or infrastructure-failure cases.

Each scenario must be generated from a known reference file and recorded
parameters. Preserve an immutable, hash-verified baseline image; run each
experiment on a separate working copy. Store outputs separately. No dataset is
generated in Phase 1.

**Requires approval:** scenario definitions, construction tools, permitted
image sizes, and exact controlled write sequences. No physical devices or
user volumes are permitted.

## G. Scoring and unscorable rules

The scoring tool must verify input hashes, candidate/reference association,
expected size, procedure version, completion status, and output provenance before
scoring. It must compare bytes deterministically and use independently documented
provenance to map each output byte to a unique original logical position. Content
equality alone is not sufficient to establish position. Count each verified
original position once. Overlapping ranges must be unioned, never summed twice.
If a unique mapping cannot be established and the ambiguity prevents a defensible
score, mark the case unscorable rather than guessing.

Use the immutable original size as denominator. A score of `0.0` is valid only
when the frozen procedure completed successfully, its inputs and identity were
validated, and it produced no verified original logical byte positions. Unsupported
filesystem or metadata, ambiguous identity, missing reference/size, zero-length
source, corrupt/mismatched input hash, malformed or ambiguous cluster mapping,
cancellation, short I/O, stale image identity, tool crash, incomplete execution,
and infrastructure failure are unscorable with explicit reason codes. Keep
procedure outcome failures distinct from infrastructure or evidence failures.
Missing evidence or a failed/incomplete tool run must never silently become
`recovery_fraction = 0.0`. Report scored and unscorable counts separately, including
unscorable reason-code counts.

## H. Data separation and leakage prevention

**Proposed split:** partition by whole disk image and related experiment group;
all copies, scenarios, revisions, and repeated content derived from a source
file/image stay in one split. Reserve an independent image/experiment group for
final testing. Do not randomly split rows from the same image or related file
content across train, validation, and test.

Before splitting, identify duplicate/repeated file content by approved hashes
and group it together. Keep reference bytes, recovery outputs, and scoring
artifacts in a label-generation path unavailable to feature extraction. Avoid
post-recovery metadata leakage. Record exact group membership, split seed, and
the rationale for grouping.

Before evaluation, explicitly state which generalization claim the test supports:
(a) new images, (b) new original source files/content, (c) new scenario families,
or (d) a combination. A held-out image split alone does not prove generalization
to unseen source content or unseen scenario families if those are shared across
splits. If the dataset cannot isolate a claimed dimension, report that limitation
and do not make that claim.

No performance metrics may be reported until an independent, leakage-resistant
evaluation set exists and has been frozen.

## I. Evaluation metrics

**Proposed metrics:** report mean absolute error (MAE) and root mean squared
error (RMSE) on scored cases, alongside a documented baseline predictor (at
minimum, the training-set mean target, computed without validation/test data).
Report scored and unscorable sample counts separately, unscorable reason-code
counts, split/grouping strategy, the intended generalization claim, feature schema,
model and training configuration, random seeds, and limitations. Any comparisons
must use the same eligible held-out cases for the model and baseline. Do not
fabricate results or set a pass threshold without a documented rationale and
human approval.

## J. Reproducibility and safety

Future generation, recovery, and scoring tools must be deterministic or record
all sources of nondeterminism and seeds. Version and hash the generator,
recovery procedure, feature schema, scorer, image files, original references,
manifests, and outputs. Use allowlisted canonical regular-image-file paths
only. Reject traversal, symlinks/reparse points, device paths, physical drive
paths, mounted-volume paths, unsupported formats, and unexpected filesystem
objects. Use checked arithmetic and bounded reads for all image offsets and
lengths. Keep immutable source images separate from working copies and
outputs. Never access physical devices or write destructively to user storage.
Any ambiguity, overflow, identity mismatch, bounds violation, short read, or
unexpected state fails closed.

This protocol does not authorize any wipe/recovery UI action, physical-device
access, or production storage operation. **Inspected fact:** `WipeEngine`
retains a `wipeVolumeExtents` path that calls the injected storage interface's
`writeAt`; the legacy raw-path/sector/extent entry points return `Unsupported`.
This document does not establish whether a production storage implementation
can be supplied or invoked. Do not enable real-device wiping; audit that path
and satisfy separate safety gates before any separately approved production
backend work.

## K. Phase gates

1. **Phase 1:** protocol reviewed and explicitly approved by the human owner.
2. **Phase 2:** controlled dataset and immutable manifest created, hash-verified,
   and reviewed; no protected baseline data is altered.
3. **Phase 3:** approved read-only recovery tool produces deterministic,
   reproducible labels with explicit unscorable handling.
4. **Phase 4:** pre-recovery feature contract, baseline comparison,
   Random-Forest evaluation, and leakage-resistant splits are validated.
5. **Phase 5:** UI integration reports an estimate with uncertainty and without
   claims beyond the evaluated procedure and population.
6. **Phase 6:** wiping logic is tested on disposable image files before any
   separately approved production-backend work.
7. **Phase 7:** the same benchmark is rerun on wiped copies for standard and
   ML post-wipe evaluation.

No later phase starts automatically. Each phase requires its own authorization
and safety review. The Phase 7 benchmark must not imply physical SSD-cell
erasure or universal unrecoverability from logical overwrite evidence.

## Open questions for protocol approval

1. Approve or revise `recovery_fraction`, including zero-length handling and
   the requirement for byte-position provenance.
2. Approve the precise FAT32 recovery algorithm, bounds, unsupported cases,
   and procedure-version policy before any label generation.
3. Approve the manifest schema, immutable storage location, and custody/hash
   verification procedure without changing existing protected artifacts.
4. Decide whether candidate-specific pre-recovery content features are
   excluded or separately designed; the current extractor API alone does not
   make them available.
5. Approve the scenario groups, split policy and intended generalization claim,
   metrics, and baseline predictor before training or performance reporting.
