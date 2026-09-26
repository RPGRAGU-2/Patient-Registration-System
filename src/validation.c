#include "prs/validation.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void clear_error(PrsValidationError *error) {
    if (error != NULL) {
        error->field = PRS_FIELD_NONE;
        error->message[0] = '\0';
    }
}

static void set_error(PrsValidationError *error, PrsPatientField field,
                      const char *message) {
    if (error != NULL) {
        error->field = field;
        (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
}

static bool is_blank(const char *value) {
    const unsigned char *cursor = (const unsigned char *)value;

    if (value == NULL) {
        return true;
    }
    while (*cursor != '\0') {
        if (!isspace(*cursor)) {
            return false;
        }
        ++cursor;
    }
    return true;
}

static bool has_valid_length(const char *value, size_t minimum, size_t maximum) {
    size_t length = 0U;

    if (value == NULL) {
        return false;
    }
    length = strlen(value);
    return length >= minimum && length <= maximum;
}

static bool is_valid_name(const char *value) {
    const unsigned char *cursor = (const unsigned char *)value;

    if (is_blank(value) || !has_valid_length(value, 1U, 100U)) {
        return false;
    }
    while (*cursor != '\0') {
        if (!(isalpha(*cursor) || *cursor >= 128U || *cursor == ' ' ||
              *cursor == '-' || *cursor == '\'' || *cursor == '.')) {
            return false;
        }
        ++cursor;
    }
    return true;
}

static bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static bool is_valid_date_of_birth(const char *value) {
    int year = 0;
    int month = 0;
    int day = 0;
    int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    time_t now;
    struct tm *local_time = NULL;

    if (value == NULL || strlen(value) != 10U ||
        !isdigit((unsigned char)value[0]) || !isdigit((unsigned char)value[1]) ||
        !isdigit((unsigned char)value[2]) || !isdigit((unsigned char)value[3]) ||
        !isdigit((unsigned char)value[5]) || !isdigit((unsigned char)value[6]) ||
        !isdigit((unsigned char)value[8]) || !isdigit((unsigned char)value[9]) ||
        sscanf(value, "%4d-%2d-%2d", &year, &month, &day) != 3 ||
        value[4] != '-' || value[7] != '-') {
        return false;
    }
    if (year < 1900 || month < 1 || month > 12) {
        return false;
    }
    if (month == 2 && is_leap_year(year)) {
        days_in_month[1] = 29;
    }
    if (day < 1 || day > days_in_month[month - 1]) {
        return false;
    }

    now = time(NULL);
    local_time = localtime(&now);
    if (local_time != NULL &&
        (year > local_time->tm_year + 1900 ||
         (year == local_time->tm_year + 1900 && month > local_time->tm_mon + 1) ||
         (year == local_time->tm_year + 1900 && month == local_time->tm_mon + 1 &&
          day > local_time->tm_mday))) {
        return false;
    }
    return true;
}

static bool is_valid_phone(const char *value) {
    const unsigned char *cursor = (const unsigned char *)value;
    unsigned int digit_count = 0U;

    if (is_blank(value) || !has_valid_length(value, 7U, 31U)) {
        return false;
    }
    while (*cursor != '\0') {
        if (isdigit(*cursor)) {
            ++digit_count;
        } else if (!(*cursor == ' ' || *cursor == '+' || *cursor == '-' ||
                     *cursor == '(' || *cursor == ')' || *cursor == '.')) {
            return false;
        }
        ++cursor;
    }
    return digit_count >= 7U && digit_count <= 20U;
}

static bool is_valid_email(const char *value) {
    size_t length = 0U;
    size_t at_index = 0U;
    bool has_at = false;
    bool has_domain_dot = false;
    size_t index = 0U;

    if (is_blank(value)) {
        return true;
    }
    length = strlen(value);
    if (length < 3U || length > 254U) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        const unsigned char character = (unsigned char)value[index];

        if (character == '@') {
            if (has_at || index == 0U || index == length - 1U) {
                return false;
            }
            has_at = true;
            at_index = index;
        } else if (!(isalnum(character) || character == '.' || character == '_' ||
                     character == '+' || character == '-')) {
            return false;
        }
    }
    if (!has_at || at_index + 2U >= length || value[at_index + 1U] == '.') {
        return false;
    }
    for (index = at_index + 1U; index < length; ++index) {
        if (value[index] == '.') {
            has_domain_dot = true;
        }
    }
    return has_domain_dot && value[length - 1U] != '.';
}

static bool has_optional_maximum_length(const char *value, size_t maximum) {
    return value != NULL && strlen(value) <= maximum;
}

const char *prs_patient_field_name(PrsPatientField field) {
    switch (field) {
        case PRS_FIELD_FIRST_NAME:
            return "First name";
        case PRS_FIELD_LAST_NAME:
            return "Last name";
        case PRS_FIELD_DATE_OF_BIRTH:
            return "Date of birth";
        case PRS_FIELD_SEX_AT_REGISTRATION:
            return "Sex at registration";
        case PRS_FIELD_PHONE:
            return "Phone";
        case PRS_FIELD_EMAIL:
            return "Email";
        case PRS_FIELD_CLINIC_IDENTIFIER:
            return "Clinic identifier";
        case PRS_FIELD_INSURANCE_REFERENCE:
            return "Insurance reference";
        case PRS_FIELD_ADDRESS:
            return "Address";
        case PRS_FIELD_EMERGENCY_CONTACT_NAME:
            return "Emergency contact name";
        case PRS_FIELD_EMERGENCY_CONTACT_PHONE:
            return "Emergency contact phone";
        case PRS_FIELD_GUARDIAN_NAME:
            return "Guardian name";
        case PRS_FIELD_GUARDIAN_PHONE:
            return "Guardian phone";
        case PRS_FIELD_PATIENT_NOTES:
            return "Patient notes";
        case PRS_FIELD_NONE:
        default:
            return "";
    }
}

bool prs_validate_patient_draft(const PrsPatientDraft *draft,
                                PrsValidationError *error) {
    clear_error(error);
    if (draft == NULL) {
        set_error(error, PRS_FIELD_NONE, "Patient data is required.");
        return false;
    }
    if (!is_valid_name(draft->first_name)) {
        set_error(error, PRS_FIELD_FIRST_NAME,
                  "Enter a first name using letters and standard name punctuation.");
        return false;
    }
    if (!is_valid_name(draft->last_name)) {
        set_error(error, PRS_FIELD_LAST_NAME,
                  "Enter a last name using letters and standard name punctuation.");
        return false;
    }
    if (!is_valid_date_of_birth(draft->date_of_birth)) {
        set_error(error, PRS_FIELD_DATE_OF_BIRTH,
                  "Enter a valid past or current date in YYYY-MM-DD format.");
        return false;
    }
    if (is_blank(draft->sex_at_registration) ||
        !has_valid_length(draft->sex_at_registration, 1U, 31U)) {
        set_error(error, PRS_FIELD_SEX_AT_REGISTRATION,
                  "Select a value for sex at registration.");
        return false;
    }
    if (!is_valid_phone(draft->phone)) {
        set_error(error, PRS_FIELD_PHONE,
                  "Enter a phone number containing 7 to 20 digits.");
        return false;
    }
    if (!is_valid_email(draft->email)) {
        set_error(error, PRS_FIELD_EMAIL, "Enter a valid email address or leave it blank.");
        return false;
    }
    if (!has_optional_maximum_length(draft->clinic_identifier, 100U)) {
        set_error(error, PRS_FIELD_CLINIC_IDENTIFIER,
                  "Clinic identifier cannot exceed 100 characters.");
        return false;
    }
    if (!has_optional_maximum_length(draft->insurance_reference, 100U)) {
        set_error(error, PRS_FIELD_INSURANCE_REFERENCE,
                  "Insurance reference cannot exceed 100 characters.");
        return false;
    }
    if (is_blank(draft->address) || !has_valid_length(draft->address, 5U, 500U)) {
        set_error(error, PRS_FIELD_ADDRESS,
                  "Enter an address between 5 and 500 characters.");
        return false;
    }
    if (!is_valid_name(draft->emergency_contact_name)) {
        set_error(error, PRS_FIELD_EMERGENCY_CONTACT_NAME,
                  "Enter an emergency contact name.");
        return false;
    }
    if (!is_valid_phone(draft->emergency_contact_phone)) {
        set_error(error, PRS_FIELD_EMERGENCY_CONTACT_PHONE,
                  "Enter a valid emergency contact phone number.");
        return false;
    }
    if (!is_blank(draft->guardian_name) && !is_valid_name(draft->guardian_name)) {
        set_error(error, PRS_FIELD_GUARDIAN_NAME,
                  "Enter a guardian name using standard name punctuation.");
        return false;
    }
    if (!is_blank(draft->guardian_name) && !is_valid_phone(draft->guardian_phone)) {
        set_error(error, PRS_FIELD_GUARDIAN_PHONE,
                  "Enter a guardian phone number containing 7 to 20 digits.");
        return false;
    }
    if (is_blank(draft->guardian_name) && !is_blank(draft->guardian_phone)) {
        set_error(error, PRS_FIELD_GUARDIAN_NAME,
                  "Enter a guardian name when a guardian phone is supplied.");
        return false;
    }
    if (!has_optional_maximum_length(draft->patient_notes, 5000U)) {
        set_error(error, PRS_FIELD_PATIENT_NOTES,
                  "Patient notes cannot exceed 5,000 characters.");
        return false;
    }
    return true;
}
