# Data Wiping and Recovery Application
## Architecture, Requirements, and Design Specification

**Document type:** Software Design Specification  
**Application type:** Desktop application  
**Primary platform:** Windows  
**Core technologies:** C++, Qt, Python (ML/assistant integration)

---

## 1. Project Overview

The Data Wiping and Recovery Application is a desktop tool for inspecting storage devices, assessing file and device information, securely wiping selected data, attempting file or drive recovery, and producing auditable operation records.

The application combines a graphical interface with a low-level storage engine and an optional machine-learning (ML) analysis layer. It is designed to make storage operations understandable to users while prioritizing safety, transparency, verification, and traceability.

### 1.1 Goals

- Detect and display connected storage devices and relevant device details.
- Inspect supported file systems for existing, hidden, and potentially deleted files.
- Offer four main workflows: full-drive wiping, single-file wiping, file recovery, and drive recovery.
- Show operation progress, elapsed time, outcomes, warnings, and errors.
- Compare pre-operation and post-operation scan results where appropriate.
- Maintain an operation history with device, target, size, duration, method, and result.
- Generate a certificate or report for each completed operation and associate it with the user's account.
- Provide an on-screen assistant that answers questions, explains findings, and guides users through suitable next steps.
- Use ML-assisted analysis where there is a validated model and sufficient evidence; clearly distinguish predictions from verified results.

### 1.2 Scope

The application focuses on supported storage devices and file systems. Actual capabilities depend on operating-system permissions, device interfaces, file-system support, storage technology, and the implementation of each operation.

The design must not claim that a file is recoverable, permanently erased, or unrecoverable solely from an ML prediction or from a scan that finds no recognizable file signatures.

---

## 2. Users and Use Cases

### 2.1 Intended users

- Users who want to inspect a connected storage device.
- Users who want to wipe a whole supported drive or a selected file.
- Users who want to scan for and recover supported deleted files.
- Users who need an operation history and evidence of what the application attempted and verified.

### 2.2 Main use cases

1. Create an account or sign in.
2. Connect and select a storage device.
3. Review device information and scan findings.
4. Select one of four operations.
5. Review the target and confirm the operation.
6. Monitor execution and review the outcome.
7. View the before/after comparison, logs, and generated certificate.
8. Ask the assistant for explanations or next-step guidance.
9. Reopen past operations and certificates from the account history.

---

## 3. High-Level Architecture

The application uses a layered desktop architecture. The Qt interface communicates with application services, which coordinate the C++ storage engine, analysis components, assistant, and persistence layer.

```text
+--------------------------------------------------------------+
|                     Desktop User Interface                  |
| Login | Dashboard | Device Details | Operations | History    |
| Certificates | Settings | Assistant Panel                   |
+------------------------------+-------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|                    Application / Workflow Layer              |
| Authentication | Device Selection | Operation Coordinator   |
| Validation | Progress Events | Error Handling | Permissions  |
+------------------------------+-------------------------------+
                               |
                +--------------+----------------+
                |                               |
                v                               v
+-------------------------------+  +----------------------------+
|       C++ Storage Engine      |  | Python Analysis Services   |
| Device Access / Enumeration   |  | ML Assessment (optional)   |
| File-System Scanning          |  | Feature Processing         |
| Wipe Engine                   |  | Assistant / Guidance       |
| Recovery Engine               |  | Recommendation Rules       |
| Verification                  |  |                            |
+---------------+---------------+  +--------------+-------------+
                |                                 |
                v                                 v
+--------------------------------------------------------------+
|                    Data and Audit Layer                       |
| User Profiles | Device/Scan Metadata | Operation Logs         |
| Certificates | Assistant Context (minimal) | Model Metadata   |
+--------------------------------------------------------------+
```

### 3.1 Frontend

**Technology:** Qt Widgets / Qt Designer, using the existing Qt/C++ application where practical.

Responsibilities:
- Display the welcome and sign-in screens.
- Present connected devices and scan results.
- Provide operation selection and confirmation screens.
- Show progress, elapsed time, status, warnings, and errors.
- Display operation history and certificates.
- Host the assistant panel.
- Keep destructive-operation controls explicit and difficult to trigger accidentally.

### 3.2 Application / workflow layer

Responsibilities:
- Coordinate UI actions and backend operations.
- Validate operation requests and targets.
- Enforce preconditions and confirmation steps.
- Send work to the relevant engine.
- Receive progress and status events.
- Handle cancellation and errors where supported.
- Initiate post-operation checks.
- Save operation records and request certificate generation.

### 3.3 C++ storage engine

Responsibilities:
- Enumerate supported storage devices and obtain device metadata.
- Scan supported file systems.
- Identify existing, hidden, and potentially deleted file entries where supported.
- Execute supported wiping operations.
- Perform read-only recovery scans and supported recovery operations.
- Verify operation outcomes using appropriate technical checks.
- Report progress, errors, and structured results to the application layer.

Raw-device access and destructive operations must be isolated behind a small, validated interface. The engine must not accept arbitrary device paths or targets without validation.

### 3.4 Python analysis and assistant services

Python is an optional supporting layer rather than a replacement for the storage engine.

Responsibilities:
- Process scan metadata and derive analysis features.
- Run a trained ML model if one exists and has been evaluated.
- Return clearly labelled predictions, confidence/uncertainty information, and limitations.
- Provide assistant responses based on application state, documented capabilities, and safe next steps.
- Offer recommendations without directly initiating destructive operations.

The UI may communicate with Python through a local process interface, such as a controlled subprocess using structured JSON messages, or through a stable native interface. The chosen interface should be documented and versioned. Errors or unavailability in the Python service must not corrupt or silently alter storage operations.

### 3.5 Persistence layer

Stores:
- User profile and authentication metadata.
- Device and scan summaries.
- Operation history and structured audit events.
- Certificate/report metadata and generated report files.
- Relevant model version and analysis metadata.
- Minimal assistant conversation context, if history is enabled.

A local database such as SQLite is suitable for a standalone prototype. A server-backed database can be introduced if multi-device account access is required.

---

## 4. End-to-End User Journey

1. **Launch:** The user opens the application and sees a welcome screen.
2. **Authentication:** The user signs in or creates an account.
3. **Device connection:** The application lists detected storage devices and asks the user to select one.
4. **Initial scan:** The application reads supported device and file-system metadata and performs the selected scan.
5. **Analysis dashboard:** The application displays device details, existing files, hidden files, potentially deleted entries, scan limitations, and any available health information.
6. **Operation selection:** The user selects one of the four workflows.
7. **Preflight checks:** The application validates the device and target, checks permissions and supported file systems, explains consequences, and asks for explicit confirmation.
8. **Execution:** The relevant engine runs and reports progress, elapsed time, and status.
9. **Post-operation checks:** The application performs appropriate verification or recovery-result checks.
10. **Summary:** The application displays the outcome and compares relevant pre-operation and post-operation findings.
11. **Audit record:** The application saves the operation details, including failures or interruptions.
12. **Certificate/report:** A report is generated from the saved record and verification evidence.
13. **History:** The user can revisit the operation and its report from the account.

---

## 5. Functional Requirements

### FR-01: Authentication and account

- The application shall provide a sign-in flow and an account identity for associating operation history and certificates.
- The application shall restrict access to account-specific records.
- Passwords must not be stored as plaintext. Use a well-established password-hashing approach if passwords are managed locally.
- If authentication is implemented only as a local demonstration, this limitation must be stated clearly.

### FR-02: Device detection

- The application shall list detected supported storage devices.
- It shall display available device information, such as device name/model, capacity, connection status, filesystem, and a stable device identifier where available.
- It shall refresh the list and detect disconnection or device changes.
- It shall identify unsupported or inaccessible devices and explain the limitation.

### FR-03: Initial device analysis

- The application shall scan supported file systems for existing entries.
- It shall identify hidden files using the relevant file-system or operating-system attributes.
- It shall identify potentially deleted entries where the implemented scanner supports the file system.
- It shall distinguish scan findings from confirmed recoverable file contents.
- It shall show scan time, scope, limitations, and errors.

### FR-04: Full-drive wiping

- The user shall be able to select a supported target device for wiping.
- The application shall clearly identify the target and warn that data may be permanently destroyed.
- The application shall require explicit confirmation immediately before execution.
- The engine shall report the selected method, progress, elapsed time, completion status, and errors.
- The application shall perform appropriate post-operation verification and record its scope and result.
- The application shall not claim a universal guarantee of irrecoverability when the hardware or storage technology prevents that conclusion.

### FR-05: Single-file wiping

- The user shall be able to select a supported file target.
- The application shall validate the target and explain the limitations of securely erasing an individual file.
- The application shall distinguish ordinary file deletion from verified secure erasure.
- Where the storage technology or file system cannot support reliable per-file erasure, the application shall state this and avoid overstating the result.

### FR-06: File recovery

- The user shall be able to scan for potentially recoverable files on supported devices.
- The application shall show available metadata, such as filename when known, size, file type, and recovery status.
- The user shall be able to select supported files for recovery.
- Recovered files should be written to a different destination device or location to reduce the risk of overwriting recoverable source data.
- The application shall report incomplete, damaged, or unsuccessful recovery attempts.

### FR-07: Drive recovery

- The user shall be able to initiate a supported drive-level recovery scan.
- The application shall report discovered file entries and recovery candidates, together with the scan scope and limitations.
- The application shall provide a destination separate from the source device for recovered data.
- The application shall not promise recovery of all deleted or damaged data.

### FR-08: Progress and operation status

- The application shall show progress when the engine can provide a meaningful progress measure.
- It shall display elapsed time and a clear status such as queued, running, completed, failed, cancelled, or interrupted.
- It shall surface errors and actionable guidance without hiding partial results.
- Cancellation must be supported only where the engine can stop safely; the UI must explain when an operation cannot be safely interrupted.

### FR-09: Before/after comparison

- The application shall preserve a summary of the initial scan.
- After an operation, it shall perform the relevant post-operation check.
- It shall compare applicable results and explain what was and was not verified.
- It shall not interpret the absence of file signatures alone as proof that all data has been erased.

### FR-10: Audit logging

Each operation record should include:
- Unique operation ID.
- Account ID or local profile reference.
- Start and end timestamps, duration, and time zone where relevant.
- Device name, capacity, identifier where available, and filesystem.
- Operation type and target file/path where applicable.
- Target size or amount of data processed, clearly distinguishing these values.
- Selected method and relevant parameters.
- Status, progress milestones, errors, warnings, and cancellation/interruption information.
- Pre-operation and post-operation scan references.
- Verification checks performed and their results.
- Application version and ML model version, if used.

Logs must avoid storing file contents or unnecessary sensitive metadata. Access to logs should be controlled, and records should not be silently rewritten after completion.

### FR-11: Certificate/report generation

- The application shall generate a report for each eligible operation.
- The report shall reference the operation ID and include target details, method, timestamps, duration, result, and verification evidence.
- It shall clearly distinguish successful execution from successful verification.
- It shall record inconclusive checks and failures rather than issuing an unconditional success certificate.
- The report shall be stored in the user's history and made available for viewing or export.
- The report's authenticity and integrity protections, if any, shall be described accurately.

### FR-12: Assistant

- The assistant shall be accessible from the main application screens.
- It shall answer questions about device details, scan results, supported operations, errors, logs, and certificates.
- It shall explain technical terms in user-friendly language.
- It shall provide context-aware next-step recommendations.
- It shall respect the currently selected device and operation context.
- It shall not directly execute wiping or recovery actions from a conversational response.
- It shall direct the user to the relevant screen and require the normal validation and confirmation flow for any operation.
- It shall disclose uncertainty and avoid unsupported claims about recoverability or erasure.
- If the assistant service is unavailable, core storage functionality shall remain usable.

---

## 6. Non-Functional Requirements

### 6.1 Safety and reliability
- Destructive actions require clear target identification and explicit confirmation.
- The application must protect system and boot drives from accidental selection by default.
- Device identity must be revalidated immediately before a destructive operation.
- Unexpected disconnection, write errors, permission failures, and application crashes must be handled and recorded.
- A failed or interrupted operation must never be reported as verified success.

### 6.2 Security and privacy
- Use least-privilege access and request elevated permissions only when necessary.
- Protect account data, logs, and certificates from unauthorized access.
- Do not store plaintext passwords.
- Validate all paths, device identifiers, process arguments, and messages crossing the C++/Python boundary.
- Avoid sending device metadata or user data to external services unless required, disclosed, and authorized.
- Keep assistant context to the minimum necessary.

### 6.3 Performance
- Long-running scans and operations shall not freeze the UI.
- Progress updates should be delivered asynchronously.
- Scanning and ML analysis should process metadata where possible rather than loading entire drives into memory.
- Memory and buffer usage should be bounded.

### 6.4 Usability and accessibility
- Use clear labels and consistent navigation.
- Show progress, status, errors, and next steps in plain language.
- Make destructive actions visually distinct from read-only scans.
- Do not rely on color alone to communicate status.
- Provide confirmation screens with the selected device and target.

### 6.5 Maintainability
- Keep UI, workflow, storage engine, analysis, assistant, and persistence responsibilities separate.
- Use structured interfaces and error codes between modules.
- Record application and model versions in diagnostic and audit data.
- Test each operation independently on safe test media.

---

## 7. User Interface Design

### 7.1 Main navigation

Recommended navigation items:
- Dashboard
- Devices
- Scan Results
- Operations
- Operation History
- Certificates
- Assistant
- Settings / Account

The assistant may also appear as a persistent side panel on relevant screens.

### 7.2 Screen descriptions

#### A. Welcome and authentication
- Application logo/name.
- Welcome message and signed-in username when authenticated.
- Sign-in/create-account controls.
- Clear local-account or online-account status.

#### B. Dashboard
- Connected-device count and selected device.
- Quick device summary.
- Latest scan status.
- Recent operations.
- Quick links to scan a device and view certificates.

#### C. Device selection and details
- Device list with name, capacity, filesystem, and status.
- Refresh action.
- Selected-device summary.
- Warnings for unsupported, inaccessible, or potentially dangerous targets.

#### D. Scan results
- Summary cards for existing files, hidden files, and potentially deleted entries.
- Search, filter, and sort controls.
- File list with available metadata.
- Recoverability assessment only when supported, with explanation of what the score means.
- Scan timestamp, scope, and limitations.

#### E. Operation selection
Four clear choices:
1. Wipe full drive.
2. Wipe a particular file.
3. Recover a file.
4. Recover a drive.

Each choice should include a brief explanation, risks, supported-target requirements, and the next action.

#### F. Operation confirmation and execution
- Device and target identity.
- Operation type and method.
- Consequences and relevant warnings.
- Explicit confirmation.
- Progress indicator, elapsed time, current status, and error details.

#### G. Results and verification
- Final operation status.
- Before/after comparison where applicable.
- Verification checks and limitations.
- Link to the audit log and certificate/report.
- Clear handling of failed or inconclusive results.

#### H. Operation history and certificates
- Searchable list of past operations.
- Filters for operation type, date, device, and status.
- Operation detail view.
- Open/export report controls.
- Clear distinction between completed, verified, failed, and inconclusive operations.

#### I. Assistant panel
- Conversational input and response area.
- Suggested questions based on the current screen.
- Links or actions that navigate to relevant screens.
- Clear indication when a response is an estimate or when information is unavailable.

---

## 8. Assistant Design

### 8.1 Purpose

The assistant is a guidance and explanation layer. It helps users understand the application, interpret scan results, troubleshoot errors, and choose appropriate next steps. It is not the storage engine and does not replace explicit user confirmation.

### 8.2 Context supplied to the assistant

Where available and permitted, the assistant may receive:
- Current screen and selected operation.
- Selected device's non-sensitive metadata.
- Scan summary and supported-file-system information.
- Current operation status and error codes.
- Relevant operation log and certificate status.
- Application help content and documented limitations.

It should not need raw file contents, passwords, or unrelated personal data to answer routine questions.

### 8.3 Example interactions

**Device analysis**
- User: “What does this scan tell me?”
- Assistant: Summarizes the findings, distinguishes deleted entries from recoverable content, and explains scan limitations.

**Operation guidance**
- User: “Which option should I choose to erase this USB drive?”
- Assistant: Explains the full-drive wiping workflow, its consequences, and the need to confirm the correct device.

**Recovery**
- User: “Why can't I recover this file?”
- Assistant: Explains possible reasons based on the available scan findings and recommends avoiding writes to the source device.

**Troubleshooting**
- User: “Why did the scan fail?”
- Assistant: Explains the recorded error and suggests safe checks, such as reconnecting the device or checking permissions.

**Certificate**
- User: “Does this certificate prove the data is unrecoverable?”
- Assistant: Explains exactly which verification checks passed and what those checks cannot prove.

### 8.4 Assistant safeguards

- The assistant may explain or recommend an operation, but must not initiate destructive operations directly.
- Device operations must pass through the normal workflow, validation, and confirmation screens.
- Responses must be grounded in actual application data where available.
- ML estimates must be labelled as predictions, not facts.
- The assistant must not fabricate scan results, progress, verification evidence, or certificates.
- If information is unavailable, it must say so.
- Core functionality must not depend on the assistant being online or available.

### 8.5 Assistant implementation options

- **Rule-based prototype:** FAQ content, decision trees, contextual tips, and known error explanations.
- **LLM-backed assistant:** A model receives a limited, structured summary of the current application context and approved help documentation.
- **Hybrid approach:** Use deterministic rules for safety-critical guidance and an LLM for natural-language explanations.

Any tool or function that can trigger an operation must be separated from conversational text generation and protected by explicit application-level authorization and confirmation.

---

## 9. ML Analysis Design

### 9.1 Purpose

ML may help interpret scan metadata and provide supporting assessments. It must not be treated as the authority for whether a wipe is complete or whether data can definitely be recovered.

### 9.2 Potential inputs

Depending on available data and the trained model:
- File-system metadata and entry status.
- File type and file size.
- Available file-signature or header/footer findings.
- Fragmentation or structural indicators, when measurable.
- Read errors and relevant device health attributes, if exposed.
- Results from prior scans, where valid and appropriately labelled.

### 9.3 Outputs

- A recoverability estimate or classification, if supported by model validation.
- Confidence or uncertainty information, where calibrated.
- Key contributing indicators, where explainable.
- Warnings about missing data or unsupported conditions.
- Model version and assessment timestamp.

### 9.4 ML limitations

- The model must be trained and evaluated against representative, labelled data.
- A percentage must not be presented as a calibrated probability unless validation supports that interpretation.
- A recoverability score must not be confused with the percentage of data physically recoverable.
- ML cannot independently prove secure erasure.
- Technical verification must be implemented separately from predictive analysis.
- If no validated model is available, display descriptive scan results or explicitly labelled rule-based assessments.

---

## 10. Operation Lifecycle and State Management

Each operation should have a defined state and recorded transitions.

```text
Created
   |
   v
Target Validation
   |
   v
Pre-Operation Analysis
   |
   v
Awaiting User Confirmation
   |
   v
Running
   |
   +------> Failed
   |
   +------> Cancelled / Interrupted
   |
   v
Post-Operation Verification
   |
   +------> Verification Inconclusive
   |
   +------> Verification Failed
   |
   v
Completed and Verified (only when checks support it)
   |
   v
Audit Record Saved
   |
   v
Report / Certificate Generated
```

The application should persist enough state to report interrupted operations accurately after a restart. A certificate must reflect the recorded state and evidence, not merely the fact that the engine returned.

---

## 11. Suggested Data Model

The following entities are a starting point for a local database.

### User
- `user_id`
- `username`
- `password_hash` or external authentication reference
- `created_at`

### Device
- `device_id`
- `display_name`
- `capacity_bytes`
- `filesystem`
- `device_identifier` (where available)
- `last_seen_at`

Device identifiers may be unavailable or change between connections; do not rely on a display name alone to identify a destructive target.

### Scan
- `scan_id`
- `device_id`
- `user_id`
- `started_at`
- `completed_at`
- `scan_scope`
- `existing_count`
- `hidden_count`
- `deleted_entry_count`
- `errors`
- `limitations`

### Operation
- `operation_id`
- `user_id`
- `device_id`
- `operation_type`
- `target_reference`
- `target_size_bytes`
- `method`
- `started_at`
- `ended_at`
- `duration_ms`
- `status`
- `error_summary`
- `verification_status`
- `pre_scan_id`
- `post_scan_id`

### AuditEvent
- `event_id`
- `operation_id`
- `timestamp`
- `event_type`
- `message`
- `structured_details`

### Certificate
- `certificate_id`
- `operation_id`
- `created_at`
- `report_path` or report reference
- `verification_summary`
- `integrity_metadata` (if implemented)

### Analysis
- `analysis_id`
- `scan_id`
- `model_name`
- `model_version`
- `assessment`
- `uncertainty` (if available)
- `created_at`

### AssistantConversation (optional)
- `conversation_id`
- `user_id`
- `created_at`

### AssistantMessage (optional)
- `message_id`
- `conversation_id`
- `timestamp`
- `role`
- `message_text`
- `context_reference` (minimal metadata only)

Use database transactions and appropriate constraints so an operation record cannot be associated with another user's account accidentally.

---

## 12. Error Handling and Edge Cases

The application should explicitly handle:
- No device connected.
- Multiple devices connected.
- Device disconnected during scanning or execution.
- Device identity changed between selection and execution.
- Unsupported filesystem or device type.
- Insufficient permissions.
- Read-only or write-protected device.
- Bad sectors or read/write errors.
- Recovery destination unavailable or identical to source.
- Insufficient destination space.
- Python or ML service unavailable.
- Invalid or corrupted model output.
- Database write failure.
- Certificate generation failure.
- Operation cancelled, interrupted, or resumed.
- Partial recovery or incomplete verification.

Errors must be visible to the user and preserved in the operation record wherever possible.

---

## 13. Testing and Validation

### Functional testing
- Authentication and account separation.
- Device enumeration and refresh.
- Existing/hidden/deleted-entry scanning on supported file systems.
- All four operation flows.
- Progress and status updates.
- Log persistence and history retrieval.
- Certificate/report generation.
- Assistant responses for supported questions and known errors.

### Safety testing
- Verify that the intended target is selected before destructive operations.
- Confirm that cancellation and disconnection are handled safely.
- Test on disposable USB media or disk images with nonessential sample data.
- Verify that recovery writes to a separate destination.
- Confirm that failed and inconclusive operations never receive a false success status.

### ML and assistant testing
- Test model outputs against labelled examples and document limitations.
- Check that uncertain predictions are described as uncertain.
- Test assistant answers against actual scan and operation records.
- Check that the assistant cannot bypass operation confirmation.
- Test behavior when the ML/assistant service is unavailable.

---

## 14. Design Principles

1. **Safety before convenience:** Destructive operations require deliberate, informed confirmation.
2. **Evidence over claims:** Logs and certificates report what was performed and verified.
3. **Separation of responsibilities:** UI, storage engine, ML, assistant, and persistence are distinct modules.
4. **Transparency:** Show scope, limitations, errors, and uncertainty.
5. **Least privilege:** Request only the permissions needed for a selected task.
6. **Privacy by design:** Store and expose only necessary user and device information.
7. **Graceful degradation:** Scanning and storage operations should remain usable if ML or assistant services fail.
8. **Traceability:** Each operation is linked to a unique record and report.

---

## 15. Summary

The proposed system is a desktop data wiping and recovery application built around a Qt interface, a C++ storage engine, optional Python-based ML analysis and assistant services, and a persistent audit/reporting layer.

The user signs in, connects a device, reviews scan findings, chooses one of four operations, confirms the target, monitors execution, reviews post-operation checks, and receives an auditable record and certificate/report. A contextual assistant explains results and guides the user without bypassing safety controls.

The system's credibility depends on clearly distinguishing discovered file entries, recovery attempts, ML predictions, completed operations, and technically verified outcomes.
