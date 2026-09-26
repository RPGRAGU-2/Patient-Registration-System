#include <stdio.h>
#include <string.h>

#include "prs/models.h"
#include "prs/validation.h"

static unsigned int failures = 0U;

#define EXPECT(condition, description)                                           \
    do {                                                                         \
        if (!(condition)) {                                                      \
            (void)fprintf(stderr, "FAIL: %s (line %d)\n", description, __LINE__); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static PrsPatientDraft valid_draft(void) {
    PrsPatientDraft draft = {0};

    (void)strcpy(draft.first_name, "Jane");
    (void)strcpy(draft.last_name, "O'Connor");
    (void)strcpy(draft.date_of_birth, "1985-04-18");
    (void)strcpy(draft.sex_at_registration, "Female");
    (void)strcpy(draft.phone, "+1 555 010 1234");
    (void)strcpy(draft.email, "jane.oconnor@example.com");
    (void)strcpy(draft.address, "42 Example Avenue");
    (void)strcpy(draft.emergency_contact_name, "Alex O'Connor");
    (void)strcpy(draft.emergency_contact_phone, "+1 555 010 9876");
    return draft;
}

int main(void) {
    PrsPatientDraft draft = valid_draft();
    PrsValidationError error = {0};

    EXPECT(prs_validate_patient_draft(&draft, &error),
           "valid patient draft should pass validation");

    (void)strcpy(draft.first_name, "");
    EXPECT(!prs_validate_patient_draft(&draft, &error),
           "blank first name should fail validation");
    EXPECT(error.field == PRS_FIELD_FIRST_NAME,
           "blank first name should identify the first-name field");

    draft = valid_draft();
    (void)strcpy(draft.date_of_birth, "2024-02-30");
    EXPECT(!prs_validate_patient_draft(&draft, &error),
           "invalid calendar date should fail validation");
    EXPECT(error.field == PRS_FIELD_DATE_OF_BIRTH,
           "invalid date should identify the date field");

    draft = valid_draft();
    (void)strcpy(draft.date_of_birth, "1985-04-1x");
    EXPECT(!prs_validate_patient_draft(&draft, &error),
           "non-numeric date component should fail validation");

    draft = valid_draft();
    (void)strcpy(draft.phone, "1234");
    EXPECT(!prs_validate_patient_draft(&draft, &error),
           "short phone number should fail validation");
    EXPECT(error.field == PRS_FIELD_PHONE,
           "short phone should identify the phone field");

    draft = valid_draft();
    (void)strcpy(draft.email, "invalid@domain");
    EXPECT(!prs_validate_patient_draft(&draft, &error),
           "email without a domain suffix should fail validation");
    EXPECT(error.field == PRS_FIELD_EMAIL,
           "invalid email should identify the email field");

    draft = valid_draft();
    (void)strcpy(draft.emergency_contact_phone, "555-abc-1234");
    EXPECT(!prs_validate_patient_draft(&draft, &error),
           "alphabetic emergency phone should fail validation");
    EXPECT(error.field == PRS_FIELD_EMERGENCY_CONTACT_PHONE,
           "invalid emergency phone should identify its field");

    if (failures == 0U) {
        (void)printf("Validation tests passed.\n");
        return 0;
    }
    (void)fprintf(stderr, "%u validation test assertion(s) failed.\n", failures);
    return 1;
}
