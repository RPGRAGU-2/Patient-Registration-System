#ifndef PRS_CLINICAL_SERVICE_H
#define PRS_CLINICAL_SERVICE_H

#include <stdbool.h>
#include <stddef.h>

#include "prs/database.h"
#include "prs/models.h"

bool prs_medical_history_add(PrsDatabase *database, const PrsUserSession *session,
                             const char *patient_id,
                             const PrsMedicalHistoryEntry *entry,
                             char *error_message, size_t error_message_size);
bool prs_medical_history_update(PrsDatabase *database, const PrsUserSession *session,
                                const char *patient_id,
                                const PrsMedicalHistoryEntry *entry,
                                char *error_message, size_t error_message_size);
bool prs_medical_history_delete(PrsDatabase *database, const PrsUserSession *session,
                                const char *patient_id,
                                char *error_message, size_t error_message_size);
bool prs_medical_history_latest(PrsDatabase *database, const PrsUserSession *session,
                                const char *patient_id,
                                PrsMedicalHistoryEntry *entry,
                                char *error_message, size_t error_message_size);
bool prs_provider_create(PrsDatabase *database, const PrsUserSession *session,
                         const char *display_name, const char *specialty,
                         long long *provider_id, char *error_message,
                         size_t error_message_size);
bool prs_appointment_create(PrsDatabase *database, const PrsUserSession *session,
                            const char *patient_id, long long provider_id,
                            const char *start_at, const char *end_at,
                            const char *reason, long long *appointment_id,
                            char *error_message, size_t error_message_size);
bool prs_appointments_for_patient(PrsDatabase *database, const PrsUserSession *session,
                                  const char *patient_id, PrsAppointment *appointments,
                                  size_t capacity, size_t *count,
                                  char *error_message, size_t error_message_size);
bool prs_appointment_cancel(PrsDatabase *database, const PrsUserSession *session,
                            long long appointment_id, const char *reason,
                            char *error_message, size_t error_message_size);
bool prs_appointment_reschedule(PrsDatabase *database,
                                const PrsUserSession *session,
                                long long appointment_id, const char *start_at,
                                const char *end_at, const char *reason,
                                char *error_message, size_t error_message_size);
bool prs_appointment_set_status(PrsDatabase *database,
                                const PrsUserSession *session,
                                long long appointment_id, const char *status,
                                const char *cancellation_reason,
                                char *error_message, size_t error_message_size);

#endif
