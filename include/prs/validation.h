#ifndef PRS_VALIDATION_H
#define PRS_VALIDATION_H

#include <stdbool.h>
#include <stddef.h>

#include "prs/models.h"

typedef enum {
    PRS_FIELD_NONE = 0,
    PRS_FIELD_FIRST_NAME,
    PRS_FIELD_LAST_NAME,
    PRS_FIELD_DATE_OF_BIRTH,
    PRS_FIELD_SEX_AT_REGISTRATION,
    PRS_FIELD_PHONE,
    PRS_FIELD_EMAIL,
    PRS_FIELD_CLINIC_IDENTIFIER,
    PRS_FIELD_INSURANCE_REFERENCE,
    PRS_FIELD_ADDRESS,
    PRS_FIELD_EMERGENCY_CONTACT_NAME,
    PRS_FIELD_EMERGENCY_CONTACT_PHONE,
    PRS_FIELD_GUARDIAN_NAME,
    PRS_FIELD_GUARDIAN_PHONE,
    PRS_FIELD_PATIENT_NOTES
} PrsPatientField;

typedef struct {
    PrsPatientField field;
    char message[160];
} PrsValidationError;

bool prs_validate_patient_draft(const PrsPatientDraft *draft,
                                PrsValidationError *error);
const char *prs_patient_field_name(PrsPatientField field);

#endif
