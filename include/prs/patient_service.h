#ifndef PRS_PATIENT_SERVICE_H
#define PRS_PATIENT_SERVICE_H

#include <stdbool.h>
#include <stddef.h>

#include "prs/database.h"
#include "prs/models.h"
#include "prs/validation.h"

typedef enum {
    PRS_PATIENT_CREATE_SAVED = 0,
    PRS_PATIENT_CREATE_DUPLICATE_REVIEW_REQUIRED,
    PRS_PATIENT_CREATE_DENIED,
    PRS_PATIENT_CREATE_INVALID,
    PRS_PATIENT_CREATE_FAILED
} PrsPatientCreateResult;

PrsPatientCreateResult prs_patient_create(
    PrsDatabase *database, const PrsUserSession *session,
    const PrsPatientDraft *draft, bool duplicate_review_acknowledged,
    char *patient_id, size_t patient_id_size, PrsValidationError *validation_error,
    PrsPatientSummary *duplicate_candidates, size_t duplicate_capacity,
    size_t *duplicate_count, char *error_message, size_t error_message_size);

bool prs_patient_get_by_id(PrsDatabase *database, const PrsUserSession *session,
                           const char *patient_id, PrsPatientRecord *patient,
                           char *error_message, size_t error_message_size);
bool prs_patient_search(PrsDatabase *database, const PrsUserSession *session,
                        const char *search_term, PrsPatientSummary *results,
                        size_t result_capacity, size_t *result_count,
                        char *error_message, size_t error_message_size);
bool prs_patient_update(PrsDatabase *database, const PrsUserSession *session,
                        const char *patient_id, const PrsPatientDraft *draft,
                        PrsValidationError *validation_error,
                        char *error_message, size_t error_message_size);
bool prs_patient_set_active(PrsDatabase *database, const PrsUserSession *session,
                            const char *patient_id, bool active,
                            char *error_message, size_t error_message_size);

#endif
