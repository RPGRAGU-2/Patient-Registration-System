#ifndef PRS_MODELS_H
#define PRS_MODELS_H

#include <stdbool.h>

#define PRS_PATIENT_ID_CAPACITY 32U
#define PRS_NAME_CAPACITY 101U
#define PRS_DATE_CAPACITY 11U
#define PRS_PHONE_CAPACITY 32U
#define PRS_EMAIL_CAPACITY 255U
#define PRS_IDENTIFIER_CAPACITY 101U
#define PRS_ADDRESS_CAPACITY 501U
#define PRS_CONTACT_NAME_CAPACITY 101U
#define PRS_MEDICAL_TEXT_CAPACITY 5001U
#define PRS_USERNAME_CAPACITY 65U
#define PRS_ROLE_CAPACITY 20U
#define PRS_DATETIME_CAPACITY 20U
#define PRS_APPOINTMENT_REASON_CAPACITY 501U

typedef struct {
    char first_name[PRS_NAME_CAPACITY];
    char last_name[PRS_NAME_CAPACITY];
    char date_of_birth[PRS_DATE_CAPACITY]; /* ISO 8601: YYYY-MM-DD */
    char sex_at_registration[32];
    char phone[PRS_PHONE_CAPACITY];
    char email[PRS_EMAIL_CAPACITY];
    char clinic_identifier[PRS_IDENTIFIER_CAPACITY];
    char insurance_reference[PRS_IDENTIFIER_CAPACITY];
    char address[PRS_ADDRESS_CAPACITY];
    char emergency_contact_name[PRS_CONTACT_NAME_CAPACITY];
    char emergency_contact_phone[PRS_PHONE_CAPACITY];
    char guardian_name[PRS_CONTACT_NAME_CAPACITY];
    char guardian_phone[PRS_PHONE_CAPACITY];
    char patient_notes[PRS_MEDICAL_TEXT_CAPACITY];
} PrsPatientDraft;

typedef enum {
    PRS_ROLE_RECEPTIONIST = 0,
    PRS_ROLE_CLINICIAN,
    PRS_ROLE_ADMINISTRATOR
} PrsUserRole;

typedef struct {
    long long user_id;
    PrsUserRole role;
    char username[PRS_USERNAME_CAPACITY];
    bool authenticated;
} PrsUserSession;

typedef struct {
    long long patient_key;
    char patient_id[PRS_PATIENT_ID_CAPACITY];
    char first_name[PRS_NAME_CAPACITY];
    char last_name[PRS_NAME_CAPACITY];
    char date_of_birth[PRS_DATE_CAPACITY];
    char phone[PRS_PHONE_CAPACITY];
    char status[10];
} PrsPatientSummary;

typedef struct {
    long long patient_key;
    char patient_id[PRS_PATIENT_ID_CAPACITY];
    PrsPatientDraft details;
    char status[10];
    char created_at[32];
    char updated_at[32];
} PrsPatientRecord;

typedef struct {
    long long history_id;
    char blood_group[10];
    char allergies[PRS_MEDICAL_TEXT_CAPACITY];
    char conditions[PRS_MEDICAL_TEXT_CAPACITY];
    char medications[PRS_MEDICAL_TEXT_CAPACITY];
    char surgeries[PRS_MEDICAL_TEXT_CAPACITY];
    char clinical_notes[PRS_MEDICAL_TEXT_CAPACITY];
    long long author_user_id;
    char created_at[32];
} PrsMedicalHistoryEntry;

typedef struct {
    long long appointment_id;
    long long patient_key;
    long long provider_id;
    char provider_name[PRS_NAME_CAPACITY];
    char start_at[PRS_DATETIME_CAPACITY];
    char end_at[PRS_DATETIME_CAPACITY];
    char reason[PRS_APPOINTMENT_REASON_CAPACITY];
    char status[16];
    char cancellation_reason[PRS_APPOINTMENT_REASON_CAPACITY];
} PrsAppointment;

#endif
