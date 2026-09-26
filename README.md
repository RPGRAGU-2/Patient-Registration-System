# Patient Registration System

This repository is a complete Phase 1 implementation of a professional patient-registration desktop application. It keeps UI, business rules, persistence, and logging separate.

## Stack

- C17
- GTK 4 for the desktop user interface
- SQLite 3 for local persistence
- Debian `libxcrypt`/yescrypt for password hashing

## Current capabilities

- Build layout, compiler-warning policy, and dependency checks
- Typed patient-registration draft and validation service
- SQLite connection configuration and versioned schema migrations
- Audit-log, user, patient, medical-history, provider, and appointment tables
- File logger that avoids writing passwords or patient content
- Administrator bootstrap and password authentication using per-password yescrypt salts
- Role-aware session state; unauthenticated users cannot retrieve or change patient data
- Patient registration service: validation, duplicate review, permanent ID reservation, storage, retrieval, search, and demographic update
- Optional clinic/insurance identifiers, guardian details, and patient notes, including identifier-based duplicate review
- Audit entries for patient creation, demographic changes, clinical records, appointments, users, and patient status changes
- Administrator service operations for creating/deactivating accounts, protecting timestamped backups, restoring validated backups, and deactivating/reactivating patient records
- Medical-history service with clinician-only write access and append-oriented history entries
- Appointment service with future-time validation, provider conflict prevention, rescheduling, status maintenance, cancellation reasons, and audit entries
- GTK login/dashboard shell wired to authentication, patient, medical-history, and appointment services; sessions lock after the configured idle period
- Dependency-free core validation tests
- SQLite integration tests for administrator setup, authentication, patient management, and access denial

## Prerequisites

Install a C17 compiler, `pkg-config`, GTK 4 development headers, SQLite 3 development headers, and `libcrypt` development headers. On Debian/Ubuntu-like systems the package names are commonly `build-essential`, `pkg-config`, `libgtk-4-dev`, `libsqlite3-dev`, and `libcrypt-dev`.

## Build and run

```sh
make test-core
make app
./build/prs --bootstrap-admin clinic-admin
./build/prs
```

`make app` checks for GTK 4 and SQLite 3 before compiling. The bootstrap command reads and confirms the first administrator password without echoing it or placing it in shell history. It can only run once; future account management must be authorized through an administrator workflow. The application creates `data/prs.db` and `logs/prs.log` when it starts. Both are intentionally ignored by Git.

Run the automated core and SQLite integration tests:

```sh
make test
```

`make test` does not need GTK 4. `make app` does, because it builds the desktop interface.

## Debian directory-tree command

From the directory where you want the project created, run this Bash command:

```sh
mkdir -p patient-registration-system/{include/prs,src,tests,docs,data,logs,build} && \
touch patient-registration-system/{Makefile,README.md,.gitignore} \
  patient-registration-system/include/prs/{app_config.h,auth.h,backup_service.h,clinical_service.h,database.h,logging.h,migrations.h,models.h,patient_service.h,ui.h,validation.h} \
  patient-registration-system/src/{main.c,app_config.c,auth.c,backup_service.c,clinical_service.c,database.c,logging.c,migrations.c,patient_service.c,ui.c,validation.c} \
  patient-registration-system/tests/{test_validation.c,test_patient_service.c}
```

## Security position for this phase

Passwords are salted and hashed with yescrypt through the system `libcrypt` implementation; the application never writes entered passwords to the database or log. Database writes require an authenticated receptionist or administrator, and patient read access requires an authenticated account. The desktop workflow supports patient registration, duplicate review, search, profile viewing, demographic edits, medical-history entry, and appointment creation/cancellation. Do not enter real patient data until a legal/privacy review, operating-system account controls, encrypted backup process, and deployment hardening are complete.

## Layout

```text
include/prs/     Public module interfaces
src/             Application implementation
tests/           Fast, dependency-free unit tests
data/            Runtime SQLite database (generated, ignored)
logs/            Runtime logs (generated, ignored)
build/           Compiled artefacts (generated, ignored)
```

See `../Phase_1_Requirements_and_Design.md` for the approved scope and design baseline.
