#include "prs/ui.h"

#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

#include "prs/auth.h"
#include "prs/app_config.h"
#include "prs/backup_service.h"
#include "prs/clinical_service.h"
#include "prs/patient_service.h"

typedef struct PrsUi PrsUi;

typedef struct {
    PrsUi *ui;
    GtkEntry *first_name, *last_name, *date_of_birth, *sex, *phone, *email;
    GtkEntry *clinic_identifier, *insurance_reference, *address;
    GtkEntry *emergency_name, *emergency_phone, *guardian_name, *guardian_phone;
    GtkEntry *patient_notes;
    GtkLabel *title, *status;
    GtkCheckButton *duplicate_acknowledgement;
    GtkButton *save;
    bool editing;
    char patient_id[PRS_PATIENT_ID_CAPACITY];
} PrsPatientForm;

struct PrsUi {
    PrsDatabase *database;
    PrsUserSession session;
    GtkStack *stack;
    GtkEntry *username;
    GtkPasswordEntry *password;
    GtkEntry *bootstrap_username;
    GtkPasswordEntry *bootstrap_password;
    GtkPasswordEntry *bootstrap_confirm;
    GtkLabel *login_status, *bootstrap_status, *dashboard_status, *search_status;
    GtkButton *bootstrap_admin;
    GtkEntry *search_entry;
    GtkListBox *search_results;
    GtkLabel *profile_details, *profile_status;
    GtkButton *profile_edit, *profile_status_change;
    GtkEntry *history_blood_group, *history_allergies, *history_conditions;
    GtkEntry *history_medications, *history_surgeries, *history_notes;
    GtkLabel *history_current, *history_status;
    GtkButton *history_save, *history_delete;
    GtkEntry *appointment_provider_id, *appointment_start, *appointment_end;
    GtkEntry *appointment_reason, *appointment_cancel_id, *appointment_cancel_reason;
    GtkLabel *appointment_list, *appointment_status;
    GtkEntry *admin_username, *admin_provider_name, *admin_specialty;
    GtkDropDown *admin_role;
    GtkPasswordEntry *admin_password;
    GtkEntry *admin_backup_directory, *admin_restore_path;
    GtkLabel *admin_status;
    PrsPatientRecord current_patient;
    bool has_current_patient;
    PrsPatientForm *patient_form;
    gint64 last_activity_usec;
};

static void show(PrsUi *ui, const char *name) {
    if (ui->session.authenticated) {
        ui->last_activity_usec = g_get_monotonic_time();
    }
    gtk_stack_set_visible_child_name(ui->stack, name);
}

static void apply_global_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css =
        "window {"
        "  background: linear-gradient(180deg, #edf3f8 0%, #dfe9f0 35%, #d4dde7 100%);"
        "  color: #1f2a37;"
        "}"
        "headerbar {"
        "  background: linear-gradient(180deg, #f4f8fb 0%, #e8eef4 100%);"
        "  border-bottom: 1px solid rgba(84, 98, 116, 0.18);"
        "  box-shadow: inset 0 -1px 0 rgba(84, 98, 116, 0.08);"
        "}"
        "button {"
        "  border: 1px solid rgba(84, 98, 116, 0.18);"
        "  border-radius: 10px;"
        "  background: linear-gradient(180deg, #f9fbfc 0%, #e7edf3 100%);"
        "  color: #1f2a37;"
        "  padding: 10px 18px;"
        "  font-weight: 600;"
        "  box-shadow: inset 0 1px 0 rgba(255,255,255,0.8);"
        "}"
        "button:hover {"
        "  background: linear-gradient(180deg, #f3f8fc 0%, #dfeaf2 100%);"
        "  border-color: rgba(54, 94, 156, 0.36);"
        "}"
        ".primary-button {"
        "  background: linear-gradient(180deg, #2d6ea8 0%, #224f86 100%);"
        "  border-color: rgba(31, 78, 136, 0.8);"
        "  color: #ffffff;"
        "  box-shadow: 0 8px 18px rgba(34, 79, 134, 0.18);"
        "}"
        ".primary-button:hover {"
        "  background: linear-gradient(180deg, #3c7ebd 0%, #245b97 100%);"
        "}"
        ".secondary-button {"
        "  background: linear-gradient(180deg, #f2f5f8 0%, #dfe8ee 100%);"
        "  border-color: rgba(84, 98, 116, 0.18);"
        "}"
        ".page-shell {"
        "  background: rgba(255, 255, 255, 0.8);"
        "  border: 1px solid rgba(84, 98, 116, 0.14);"
        "  border-radius: 22px;"
        "  box-shadow: 0 12px 28px rgba(31, 42, 55, 0.08);"
        "  padding: 18px;"
        "}"
        ".profile-details-box {"
        "  background: rgba(248, 250, 252, 0.9);"
        "  border: 1px solid rgba(84, 98, 116, 0.14);"
        "  border-radius: 14px;"
        "  padding: 16px 18px;"
        "  color: #1f2a37;"
        "  box-shadow: inset 0 1px 0 rgba(255,255,255,0.9);"
        "}"
        ".card {"
        "  background: rgba(255, 255, 255, 0.9);"
        "  border: 1px solid rgba(84, 98, 116, 0.14);"
        "  border-radius: 18px;"
        "  box-shadow: 0 12px 24px rgba(31, 42, 55, 0.06);"
        "  padding: 20px;"
        "}"
        ".title-1 {"
        "  color: #1d2d3a;"
        "  font-size: 30px;"
        "  font-weight: 700;"
        "  letter-spacing: 0.02em;"
        "}"
        ".title-2 {"
        "  color: #223448;"
        "  font-size: 22px;"
        "  font-weight: 700;"
        "  letter-spacing: 0.01em;"
        "}"
        "entry, passwordentry {"
        "  background: rgba(255, 255, 255, 0.9);"
        "  color: #1f2a37;"
        "  border: 1px solid rgba(84, 98, 116, 0.25);"
        "  border-radius: 10px;"
        "  padding: 8px 10px;"
        "  box-shadow: inset 0 1px 2px rgba(15, 23, 42, 0.04);"
        "}"
        "entry:focus, passwordentry:focus {"
        "  border-color: rgba(45, 110, 168, 0.8);"
        "  box-shadow: 0 0 0 2px rgba(45, 110, 168, 0.12);"
        "}"
        "label {"
        "  color: #2d3a4a;"
        "}"
        ".error {"
        "  color: #b42318;"
        "  font-weight: 600;"
        "}"
        "checkbutton {"
        "  color: #2d3a4a;"
        "}"
        "grid {"
        "  background: transparent;"
        "}"
        "listbox {"
        "  background: rgba(244, 247, 250, 0.9);"
        "  border: 1px solid rgba(84, 98, 116, 0.14);"
        "  border-radius: 12px;"
        "  padding: 4px;"
        "}"
        "row {"
        "  background: #f9fbfd;"
        "  color: #1f2a37;"
        "  border-bottom: 1px solid rgba(84, 98, 116, 0.14);"
        "  padding: 4px 0;"
        "}"
        "row:hover {"
        "  background: rgba(45, 110, 168, 0.09);"
        "  color: #1f2a37;"
        "}"
        ".search-result-label {"
        "  color: #1f2a37;"
        "  font-weight: 500;"
        "  padding: 10px 12px;"
        "  background: transparent;"
        "}"
        "scrolledwindow {"
        "  background: transparent;"
        "}"
        "viewport {"
        "  background: transparent;"
        "}";

    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static gboolean on_session_timer(gpointer data) {
    PrsUi *ui = data;
    const gint64 timeout = (gint64)PRS_DEFAULT_CONFIG.idle_lock_minutes * 60 * G_USEC_PER_SEC;

    if (ui->session.authenticated && ui->last_activity_usec > 0 &&
        g_get_monotonic_time() - ui->last_activity_usec >= timeout) {
        prs_session_clear(&ui->session);
        ui->has_current_patient = false;
        (void)memset(&ui->current_patient, 0, sizeof(ui->current_patient));
        gtk_editable_set_text(GTK_EDITABLE(ui->password), "");
        gtk_label_set_text(ui->login_status,
                           "Your session was locked after inactivity. Sign in to continue.");
        gtk_stack_set_visible_child_name(ui->stack, "login");
    }
    return G_SOURCE_CONTINUE;
}

static bool may_edit(const PrsUi *ui) {
    return prs_session_has_role(&ui->session, PRS_ROLE_RECEPTIONIST) ||
           prs_session_has_role(&ui->session, PRS_ROLE_ADMINISTRATOR);
}

static void add_row(GtkGrid *grid, int row, const char *label, GtkWidget *control) {
    GtkWidget *caption = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(caption), 0.0F);
    gtk_widget_set_halign(caption, GTK_ALIGN_START);
    gtk_widget_set_hexpand(control, TRUE);
    gtk_grid_attach(grid, caption, 0, row, 1, 1);
    gtk_grid_attach(grid, control, 1, row, 1, 1);
}

static void copy_entry(char *target, size_t target_size, GtkEntry *entry) {
    g_strlcpy(target, gtk_editable_get_text(GTK_EDITABLE(entry)), target_size);
}

static void set_entry(GtkEntry *entry, const char *value) {
    gtk_editable_set_text(GTK_EDITABLE(entry), value != NULL ? value : "");
}

static void on_sign_in(GtkButton *button, gpointer data) {
    PrsUi *ui = data;
    char error[256] = {0};
    char message[180];
    (void)button;
    if (!prs_authenticate(ui->database,
                          gtk_editable_get_text(GTK_EDITABLE(ui->username)),
                          gtk_editable_get_text(GTK_EDITABLE(ui->password)),
                          &ui->session, error, sizeof(error))) {
        gtk_label_set_text(ui->login_status, error);
        return;
    }
    gtk_editable_set_text(GTK_EDITABLE(ui->password), "");
    gtk_label_set_text(ui->login_status, "");
    (void)g_snprintf(message, sizeof(message), "Signed in as %s.",
                     ui->session.username);
    gtk_label_set_text(ui->dashboard_status, message);
    show(ui, "dashboard");
}

static bool has_admin_account(const PrsUi *ui) {
    sqlite3_stmt *statement = NULL;
    int status = SQLITE_ERROR;
    bool has_admin = false;

    if (ui == NULL || ui->database == NULL || ui->database->connection == NULL) {
        return false;
    }
    status = sqlite3_prepare_v2(
        ui->database->connection,
        "SELECT 1 FROM users WHERE role = 'ADMINISTRATOR' AND active = 1 LIMIT 1;",
        -1, &statement, NULL);
    if (status == SQLITE_OK) {
        status = sqlite3_step(statement);
        has_admin = status == SQLITE_ROW;
    }
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    return has_admin;
}

static void on_bootstrap_admin(GtkButton *button, gpointer data) {
    PrsUi *ui = data;
    const char *username = gtk_editable_get_text(GTK_EDITABLE(ui->bootstrap_username));
    const char *password = gtk_editable_get_text(GTK_EDITABLE(ui->bootstrap_password));
    const char *confirmation = gtk_editable_get_text(GTK_EDITABLE(ui->bootstrap_confirm));
    char error[256] = {0};
    (void)button;

    if (username[0] == '\0' || password[0] == '\0') {
        gtk_label_set_text(ui->bootstrap_status, "Enter a username and password for the first administrator.");
        return;
    }
    if (strcmp(password, confirmation) != 0) {
        gtk_label_set_text(ui->bootstrap_status, "The password and confirmation must match.");
        return;
    }
    if (!prs_auth_bootstrap_administrator(ui->database, username, password, error,
                                          sizeof(error))) {
        gtk_label_set_text(ui->bootstrap_status, error[0] != '\0' ? error : "Initial administrator setup failed.");
        return;
    }
    gtk_editable_set_text(GTK_EDITABLE(ui->bootstrap_username), "");
    gtk_editable_set_text(GTK_EDITABLE(ui->bootstrap_password), "");
    gtk_editable_set_text(GTK_EDITABLE(ui->bootstrap_confirm), "");
    gtk_label_set_text(ui->bootstrap_status, "Administrator account created. Sign in below.");
    gtk_widget_set_visible(GTK_WIDGET(ui->bootstrap_username), FALSE);
    gtk_widget_set_visible(GTK_WIDGET(ui->bootstrap_password), FALSE);
    gtk_widget_set_visible(GTK_WIDGET(ui->bootstrap_confirm), FALSE);
    gtk_widget_set_visible(GTK_WIDGET(ui->bootstrap_admin), FALSE);
}

static void on_sign_out(GtkButton *button, gpointer data) {
    PrsUi *ui = data;
    (void)button;
    prs_session_clear(&ui->session);
    ui->has_current_patient = false;
    (void)memset(&ui->current_patient, 0, sizeof(ui->current_patient));
    gtk_editable_set_text(GTK_EDITABLE(ui->username), "");
    gtk_editable_set_text(GTK_EDITABLE(ui->password), "");
    ui->last_activity_usec = 0;
    show(ui, "login");
}

static void on_dashboard(GtkButton *button, gpointer data) {
    (void)button;
    show((PrsUi *)data, "dashboard");
}

static void on_back_to_profile(GtkButton *button, gpointer data) {
    (void)button;
    show((PrsUi *)data, "profile");
}

static void open_patient_form(PrsUi *ui, bool editing) {
    PrsPatientForm *form = ui->patient_form;
    const PrsPatientDraft *details = &ui->current_patient.details;
    form->editing = editing;
    gtk_check_button_set_active(form->duplicate_acknowledgement, FALSE);
    gtk_widget_set_visible(GTK_WIDGET(form->duplicate_acknowledgement), FALSE);
    gtk_label_set_text(form->status, "");
    if (!ui->session.authenticated) {
        gtk_label_set_text(ui->dashboard_status, "Sign in to register patients.");
        show(ui, "login");
        return;
    }
    if (editing) {
        g_strlcpy(form->patient_id, ui->current_patient.patient_id,
                  sizeof(form->patient_id));
        set_entry(form->first_name, details->first_name);
        set_entry(form->last_name, details->last_name);
        set_entry(form->date_of_birth, details->date_of_birth);
        set_entry(form->sex, details->sex_at_registration);
        set_entry(form->phone, details->phone);
        set_entry(form->email, details->email);
        set_entry(form->clinic_identifier, details->clinic_identifier);
        set_entry(form->insurance_reference, details->insurance_reference);
        set_entry(form->address, details->address);
        set_entry(form->emergency_name, details->emergency_contact_name);
        set_entry(form->emergency_phone, details->emergency_contact_phone);
        set_entry(form->guardian_name, details->guardian_name);
        set_entry(form->guardian_phone, details->guardian_phone);
        set_entry(form->patient_notes, details->patient_notes);
        gtk_label_set_text(form->title, "Edit patient details");
        gtk_button_set_label(form->save, "Save changes");
    } else {
        (void)memset(form->patient_id, 0, sizeof(form->patient_id));
        set_entry(form->first_name, ""); set_entry(form->last_name, "");
        set_entry(form->date_of_birth, ""); set_entry(form->sex, "");
        set_entry(form->phone, ""); set_entry(form->email, "");
        set_entry(form->clinic_identifier, ""); set_entry(form->insurance_reference, "");
        set_entry(form->address, ""); set_entry(form->emergency_name, "");
        set_entry(form->emergency_phone, "");
        set_entry(form->guardian_name, ""); set_entry(form->guardian_phone, "");
        set_entry(form->patient_notes, "");
        gtk_label_set_text(form->title, "Register new patient");
        gtk_button_set_label(form->save, "Save patient");
    }
    show(ui, "patient-form");
}

static void on_register(GtkButton *button, gpointer data) {
    PrsUi *ui = data;
    (void)button;
    if (!ui->session.authenticated) {
        gtk_label_set_text(ui->dashboard_status, "Sign in before registering a patient.");
        show(ui, "login");
        return;
    }
    if (!may_edit(ui)) {
        gtk_label_set_text(ui->dashboard_status,
                           "Only a receptionist or administrator can register patients.");
        return;
    }
    open_patient_form(ui, false);
}

static void show_profile(PrsUi *ui, const PrsPatientRecord *patient) {
    char text[1500];
    ui->current_patient = *patient;
    ui->has_current_patient = true;
    (void)g_snprintf(text, sizeof(text),
                     "Patient ID: %s\n\nName: %s %s\nDate of birth: %s\n"
                     "Sex at registration: %s\nPhone: %s\nEmail: %s\n"
                     "Clinic identifier: %s\nInsurance reference: %s\nAddress: %s\n\n"
                     "Emergency contact: %s · %s\nGuardian: %s · %s\nNotes: %s\n"
                     "Record status: %s\nCreated: %s\nLast updated: %s",
                     patient->patient_id, patient->details.first_name,
                     patient->details.last_name, patient->details.date_of_birth,
                     patient->details.sex_at_registration, patient->details.phone,
                     patient->details.email[0] != '\0' ? patient->details.email : "Not recorded",
                     patient->details.clinic_identifier[0] != '\0' ? patient->details.clinic_identifier : "Not recorded",
                     patient->details.insurance_reference[0] != '\0' ? patient->details.insurance_reference : "Not recorded",
                     patient->details.address, patient->details.emergency_contact_name,
                     patient->details.emergency_contact_phone,
                     patient->details.guardian_name[0] != '\0' ? patient->details.guardian_name : "Not recorded",
                     patient->details.guardian_phone[0] != '\0' ? patient->details.guardian_phone : "Not recorded",
                     patient->details.patient_notes[0] != '\0' ? patient->details.patient_notes : "Not recorded",
                     patient->status,
                     patient->created_at, patient->updated_at);
    gtk_label_set_text(ui->profile_details, text);
    gtk_label_set_text(ui->profile_status, "");
    gtk_widget_set_sensitive(GTK_WIDGET(ui->profile_edit), may_edit(ui));
    gtk_button_set_label(ui->profile_status_change,
                         strcmp(patient->status, "ACTIVE") == 0 ? "Deactivate record"
                                                                 : "Reactivate record");
    gtk_widget_set_sensitive(GTK_WIDGET(ui->profile_status_change),
                             prs_session_has_role(&ui->session, PRS_ROLE_ADMINISTRATOR));
    show(ui, "profile");
}

static void on_save_patient(GtkButton *button, gpointer data) {
    PrsPatientForm *form = data;
    PrsUi *ui = form->ui;
    PrsPatientDraft draft = {0};
    PrsValidationError validation = {0};
    PrsPatientSummary duplicates[5] = {0};
    PrsPatientRecord patient = {0};
    char id[PRS_PATIENT_ID_CAPACITY] = {0};
    char error[256] = {0};
    char message[640];
    size_t duplicate_count = 0U;
    (void)button;
    copy_entry(draft.first_name, sizeof(draft.first_name), form->first_name);
    copy_entry(draft.last_name, sizeof(draft.last_name), form->last_name);
    copy_entry(draft.date_of_birth, sizeof(draft.date_of_birth), form->date_of_birth);
    copy_entry(draft.sex_at_registration, sizeof(draft.sex_at_registration), form->sex);
    copy_entry(draft.phone, sizeof(draft.phone), form->phone);
    copy_entry(draft.email, sizeof(draft.email), form->email);
    copy_entry(draft.clinic_identifier, sizeof(draft.clinic_identifier),
               form->clinic_identifier);
    copy_entry(draft.insurance_reference, sizeof(draft.insurance_reference),
               form->insurance_reference);
    copy_entry(draft.address, sizeof(draft.address), form->address);
    copy_entry(draft.emergency_contact_name, sizeof(draft.emergency_contact_name),
               form->emergency_name);
    copy_entry(draft.emergency_contact_phone, sizeof(draft.emergency_contact_phone),
               form->emergency_phone);
    copy_entry(draft.guardian_name, sizeof(draft.guardian_name), form->guardian_name);
    copy_entry(draft.guardian_phone, sizeof(draft.guardian_phone), form->guardian_phone);
    copy_entry(draft.patient_notes, sizeof(draft.patient_notes), form->patient_notes);
    if (!ui->session.authenticated) {
        gtk_label_set_text(form->status, "Sign in before saving patient information.");
        return;
    }
    if (form->editing) {
        if (!prs_patient_update(ui->database, &ui->session, form->patient_id, &draft,
                                &validation, error, sizeof(error))) {
            if (validation.field != PRS_FIELD_NONE) {
                (void)g_snprintf(message, sizeof(message), "%s: %s",
                                 prs_patient_field_name(validation.field), validation.message);
                gtk_label_set_text(form->status, message);
            } else {
                gtk_label_set_text(form->status, error);
            }
            return;
        }
        if (prs_patient_get_by_id(ui->database, &ui->session, form->patient_id,
                                  &patient, error, sizeof(error))) {
            show_profile(ui, &patient);
        } else {
            gtk_label_set_text(form->status, error);
        }
        return;
    }
    switch (prs_patient_create(ui->database, &ui->session, &draft,
                               gtk_check_button_get_active(form->duplicate_acknowledgement),
                               id, sizeof(id), &validation, duplicates,
                               sizeof(duplicates) / sizeof(duplicates[0]),
                               &duplicate_count, error, sizeof(error))) {
        case PRS_PATIENT_CREATE_SAVED:
            if (prs_patient_get_by_id(ui->database, &ui->session, id, &patient, error,
                                      sizeof(error))) {
                show_profile(ui, &patient);
            } else {
                gtk_label_set_text(form->status, error);
            }
            break;
        case PRS_PATIENT_CREATE_DUPLICATE_REVIEW_REQUIRED:
            (void)g_snprintf(message, sizeof(message),
                             "%zu possible duplicate%s found. Search first, then confirm "
                             "only if this is a new patient.", duplicate_count,
                             duplicate_count == 1U ? "" : "s");
            gtk_label_set_text(form->status, message);
            gtk_widget_set_visible(GTK_WIDGET(form->duplicate_acknowledgement), TRUE);
            break;
        case PRS_PATIENT_CREATE_INVALID:
            (void)g_snprintf(message, sizeof(message), "%s: %s",
                             prs_patient_field_name(validation.field), validation.message);
            gtk_label_set_text(form->status, message);
            break;
        default:
            gtk_label_set_text(form->status, error);
            break;
    }
}

static void on_form_cancel(GtkButton *button, gpointer data) {
    PrsPatientForm *form = data;
    (void)button;
    show(form->ui, form->editing ? "profile" : "dashboard");
}

static void on_edit_profile(GtkButton *button, gpointer data) {
    PrsUi *ui = data;
    (void)button;
    if (ui->has_current_patient && may_edit(ui)) {
        open_patient_form(ui, true);
    }
}

static void on_change_patient_status(GtkButton *button, gpointer data) {
    PrsUi *ui = data;
    PrsPatientRecord refreshed = {0};
    char error[256] = {0};
    const bool make_active = strcmp(ui->current_patient.status, "ACTIVE") != 0;
    (void)button;
    if (!prs_patient_set_active(ui->database, &ui->session,
                                ui->current_patient.patient_id, make_active, error,
                                sizeof(error))) {
        gtk_label_set_text(ui->profile_status, error);
        return;
    }
    if (prs_patient_get_by_id(ui->database, &ui->session, ui->current_patient.patient_id,
                              &refreshed, error, sizeof(error))) {
        show_profile(ui, &refreshed);
    } else {
        gtk_label_set_text(ui->profile_status, error);
    }
}

static void clear_results(PrsUi *ui) {
    GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(ui->search_results));
    while (child != NULL) {
        gtk_list_box_remove(ui->search_results, child);
        child = gtk_widget_get_first_child(GTK_WIDGET(ui->search_results));
    }
}

static void on_search_row(GtkListBox *list, GtkListBoxRow *row, gpointer data) {
    PrsUi *ui = data;
    const char *id = g_object_get_data(G_OBJECT(row), "patient-id");
    PrsPatientRecord patient = {0};
    char error[256] = {0};
    (void)list;
    if (id != NULL && prs_patient_get_by_id(ui->database, &ui->session, id, &patient,
                                            error, sizeof(error))) {
        show_profile(ui, &patient);
    } else {
        gtk_label_set_text(ui->search_status, error);
    }
}

static void append_search_result(GtkListBox *list, const PrsPatientSummary *result) {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *label = gtk_label_new(NULL);
    char text[420];

    (void)g_snprintf(text, sizeof(text), "%s · %s %s · DOB %s · %s",
                     result->patient_id, result->first_name,
                     result->last_name, result->date_of_birth,
                     result->phone);
    gtk_label_set_text(GTK_LABEL(label), text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_label_set_wrap(GTK_LABEL(label), FALSE);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_single_line_mode(GTK_LABEL(label), TRUE);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_widget_add_css_class(label, "search-result-label");
    gtk_widget_set_margin_top(label, 6);
    gtk_widget_set_margin_bottom(label, 6);
    gtk_widget_set_margin_start(label, 10);
    gtk_widget_set_margin_end(label, 10);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
    g_object_set_data_full(G_OBJECT(row), "patient-id",
                          g_strdup(result->patient_id), g_free);
    gtk_list_box_append(list, row);
}

static void on_search(GtkButton *button, gpointer data) {
    PrsUi *ui = data;
    PrsPatientSummary results[50] = {0};
    char error[256] = {0};
    size_t count = 0U, index = 0U;
    (void)button;
    clear_results(ui);
    if (!prs_patient_search(ui->database, &ui->session,
                            gtk_editable_get_text(GTK_EDITABLE(ui->search_entry)), results,
                            sizeof(results) / sizeof(results[0]), &count, error,
                            sizeof(error))) {
        gtk_label_set_text(ui->search_status, error);
        return;
    }
    if (count == 0U) {
        gtk_label_set_text(ui->search_status, "No active patient records matched your search.");
        return;
    }
    (void)g_snprintf(error, sizeof(error), "%zu matching patient record%s. Select one to open it.",
                     count, count == 1U ? "" : "s");
    gtk_label_set_text(ui->search_status, error);
    for (index = 0U; index < count && index < sizeof(results) / sizeof(results[0]); ++index) {
        append_search_result(ui->search_results, &results[index]);
    }
}

static void on_open_search(GtkButton *button, gpointer data) {
    PrsUi *ui = data;
    PrsPatientSummary results[50] = {0};
    char error[256] = {0};
    size_t count = 0U;
    (void)button;
    gtk_editable_set_text(GTK_EDITABLE(ui->search_entry), "");
    clear_results(ui);
    if (!prs_patient_search(ui->database, &ui->session, "", results,
                            sizeof(results) / sizeof(results[0]), &count,
                            error, sizeof(error))) {
        gtk_label_set_text(ui->search_status, error);
    } else if (count == 0U) {
        gtk_label_set_text(ui->search_status, "No active patient records are registered yet.");
    } else {
        (void)g_snprintf(error, sizeof(error), "%zu registered patient%s visible. Select one to open it.",
                         count, count == 1U ? "" : "s");
        gtk_label_set_text(ui->search_status, error);
        for (size_t index = 0U; index < count; ++index) {
            append_search_result(ui->search_results, &results[index]);
        }
    }
    show(ui, "search");
}

static bool may_edit_history(const PrsUi *ui) {
    return prs_session_has_role(&ui->session, PRS_ROLE_CLINICIAN) ||
           prs_session_has_role(&ui->session, PRS_ROLE_ADMINISTRATOR);
}

static bool may_manage_appointments(const PrsUi *ui) {
    return prs_session_has_role(&ui->session, PRS_ROLE_RECEPTIONIST) ||
           prs_session_has_role(&ui->session, PRS_ROLE_ADMINISTRATOR);
}

static void refresh_medical_history(PrsUi *ui) {
    PrsMedicalHistoryEntry entry = {0};
    char error[256] = {0};
    char text[1600];
    bool has_existing = false;

    has_existing = prs_medical_history_latest(ui->database, &ui->session,
                                            ui->current_patient.patient_id, &entry, error,
                                            sizeof(error));
    if (!has_existing) {
        gtk_label_set_text(ui->history_current, "Not recorded.");
        gtk_label_set_text(ui->history_status, error[0] != '\0' ? error : "No medical history has been recorded.");
        set_entry(ui->history_blood_group, ""); set_entry(ui->history_allergies, "");
        set_entry(ui->history_conditions, ""); set_entry(ui->history_medications, "");
        set_entry(ui->history_surgeries, ""); set_entry(ui->history_notes, "");
        gtk_button_set_label(ui->history_save, "Add history entry");
        gtk_widget_set_sensitive(GTK_WIDGET(ui->history_delete), FALSE);
        return;
    }
    (void)g_snprintf(text, sizeof(text),
                     "Last updated: %s by user #%lld\nBlood group: %s\nAllergies: %s\n"
                     "Conditions: %s\nMedications: %s\nSurgeries: %s\nClinical notes: %s",
                     entry.created_at, entry.author_user_id,
                     entry.blood_group[0] != '\0' ? entry.blood_group : "Not recorded",
                     entry.allergies[0] != '\0' ? entry.allergies : "Not recorded",
                     entry.conditions[0] != '\0' ? entry.conditions : "Not recorded",
                     entry.medications[0] != '\0' ? entry.medications : "Not recorded",
                     entry.surgeries[0] != '\0' ? entry.surgeries : "Not recorded",
                     entry.clinical_notes[0] != '\0' ? entry.clinical_notes : "Not recorded");
    gtk_label_set_text(ui->history_current, text);
    gtk_label_set_text(ui->history_status, "");
    set_entry(ui->history_blood_group, entry.blood_group);
    set_entry(ui->history_allergies, entry.allergies);
    set_entry(ui->history_conditions, entry.conditions);
    set_entry(ui->history_medications, entry.medications);
    set_entry(ui->history_surgeries, entry.surgeries);
    set_entry(ui->history_notes, entry.clinical_notes);
    gtk_button_set_label(ui->history_save, "Update history");
    gtk_widget_set_sensitive(GTK_WIDGET(ui->history_delete), TRUE);
}

static void on_open_medical(GtkButton *button, gpointer data) {
    PrsUi *ui = data;
    (void)button;
    if (!ui->has_current_patient) {
        gtk_label_set_text(ui->dashboard_status, "Select a patient from search first.");
        return;
    }
    if (!may_edit_history(ui)) {
        gtk_label_set_text(ui->profile_status,
                           "Medical history is available only to clinicians and administrators.");
        return;
    }
    refresh_medical_history(ui);
    show(ui, "medical-history");
}

static void on_save_medical_history(GtkButton *button, gpointer data) {
    PrsUi *ui = data;
    PrsMedicalHistoryEntry entry = {0};
    PrsMedicalHistoryEntry existing = {0};
    char error[256] = {0};
    bool has_existing = false;
    (void)button;
    copy_entry(entry.blood_group, sizeof(entry.blood_group), ui->history_blood_group);
    copy_entry(entry.allergies, sizeof(entry.allergies), ui->history_allergies);
    copy_entry(entry.conditions, sizeof(entry.conditions), ui->history_conditions);
    copy_entry(entry.medications, sizeof(entry.medications), ui->history_medications);
    copy_entry(entry.surgeries, sizeof(entry.surgeries), ui->history_surgeries);
    copy_entry(entry.clinical_notes, sizeof(entry.clinical_notes), ui->history_notes);
    has_existing = prs_medical_history_latest(ui->database, &ui->session,
                                            ui->current_patient.patient_id, &existing, error,
                                            sizeof(error));
    if (has_existing) {
        if (!prs_medical_history_update(ui->database, &ui->session,
                                       ui->current_patient.patient_id, &entry, error,
                                       sizeof(error))) {
            gtk_label_set_text(ui->history_status, error);
            return;
        }
    } else if (!prs_medical_history_add(ui->database, &ui->session,
                                        ui->current_patient.patient_id, &entry, error,
                                        sizeof(error))) {
        gtk_label_set_text(ui->history_status, error);
        return;
    }
    refresh_medical_history(ui);
}

static void on_delete_medical_history(GtkButton *button, gpointer data) {
    PrsUi *ui = data;
    char error[256] = {0};
    (void)button;

    if (!prs_medical_history_delete(ui->database, &ui->session,
                                   ui->current_patient.patient_id, error,
                                   sizeof(error))) {
        gtk_label_set_text(ui->history_status, error);
        return;
    }
    refresh_medical_history(ui);
    gtk_label_set_text(ui->history_status, "Medical history deleted.");
}

static void refresh_appointments(PrsUi *ui) {
    PrsAppointment appointments[50] = {0};
    char error[256] = {0};
    GString *text = g_string_new(NULL);
    size_t count = 0U;
    size_t index = 0U;

    if (!prs_appointments_for_patient(ui->database, &ui->session,
                                      ui->current_patient.patient_id, appointments,
                                      sizeof(appointments) / sizeof(appointments[0]),
                                      &count, error, sizeof(error))) {
        gtk_label_set_text(ui->appointment_list, "Unable to load appointments.");
        gtk_label_set_text(ui->appointment_status, error);
        g_string_free(text, TRUE);
        return;
    }
    if (count == 0U) {
        g_string_append(text, "No appointments recorded.");
    }
    for (index = 0U; index < count && index < 50U; ++index) {
        (void)g_string_append_printf(text, "#%lld · %s · %s–%s · %s\n",
                                     appointments[index].appointment_id,
                                     appointments[index].provider_name,
                                     appointments[index].start_at,
                                     appointments[index].end_at,
                                     appointments[index].status);
    }
    gtk_label_set_text(ui->appointment_list, text->str);
    gtk_label_set_text(ui->appointment_status, "");
    g_string_free(text, TRUE);
}

static void on_open_appointments(GtkButton *button, gpointer data) {
    PrsUi *ui = data;
    (void)button;
    if (!ui->has_current_patient) {
        gtk_label_set_text(ui->dashboard_status, "Select a patient from search first.");
        return;
    }
    refresh_appointments(ui);
    show(ui, "appointments");
}

static void on_create_appointment(GtkButton *button, gpointer data) {
    PrsUi *ui = data;
    char error[256] = {0};
    char *end = NULL;
    long long provider_id;
    (void)button;
    provider_id = g_ascii_strtoll(gtk_editable_get_text(GTK_EDITABLE(ui->appointment_provider_id)),
                                  &end, 10);
    if (end == NULL || *end != '\0' || provider_id <= 0 || !may_manage_appointments(ui) ||
        !prs_appointment_create(ui->database, &ui->session, ui->current_patient.patient_id,
                                provider_id,
                                gtk_editable_get_text(GTK_EDITABLE(ui->appointment_start)),
                                gtk_editable_get_text(GTK_EDITABLE(ui->appointment_end)),
                                gtk_editable_get_text(GTK_EDITABLE(ui->appointment_reason)),
                                NULL, error, sizeof(error))) {
        gtk_label_set_text(ui->appointment_status,
                           error[0] != '\0' ? error : "Enter a valid provider ID.");
        return;
    }
    set_entry(ui->appointment_provider_id, ""); set_entry(ui->appointment_start, "");
    set_entry(ui->appointment_end, ""); set_entry(ui->appointment_reason, "");
    refresh_appointments(ui);
}

static void on_cancel_appointment(GtkButton *button, gpointer data) {
    PrsUi *ui = data;
    char error[256] = {0};
    char *end = NULL;
    long long appointment_id;
    (void)button;
    appointment_id = g_ascii_strtoll(gtk_editable_get_text(GTK_EDITABLE(ui->appointment_cancel_id)),
                                     &end, 10);
    if (end == NULL || *end != '\0' || appointment_id <= 0 || !may_manage_appointments(ui) ||
        !prs_appointment_cancel(ui->database, &ui->session, appointment_id,
                                gtk_editable_get_text(GTK_EDITABLE(ui->appointment_cancel_reason)),
                                error, sizeof(error))) {
        gtk_label_set_text(ui->appointment_status,
                           error[0] != '\0' ? error : "Enter a valid appointment ID.");
        return;
    }
    set_entry(ui->appointment_cancel_id, ""); set_entry(ui->appointment_cancel_reason, "");
    refresh_appointments(ui);
}

static void on_open_administration(GtkButton *button, gpointer data) {
    PrsUi *ui = data;
    (void)button;
    if (!prs_session_has_role(&ui->session, PRS_ROLE_ADMINISTRATOR)) {
        gtk_label_set_text(ui->dashboard_status,
                           "Administration is available only to administrators.");
        return;
    }
    gtk_label_set_text(ui->admin_status, "");
    show(ui, "administration");
}

static bool admin_role_from_index(guint selected_index, PrsUserRole *role) {
    switch (selected_index) {
        case 0U:
            *role = PRS_ROLE_RECEPTIONIST;
            return true;
        case 1U:
            *role = PRS_ROLE_CLINICIAN;
            return true;
        case 2U:
            *role = PRS_ROLE_ADMINISTRATOR;
            return true;
        default:
            return false;
    }
}

static void on_create_user(GtkButton *button, gpointer data) {
    PrsUi *ui = data;
    PrsUserRole role;
    char error[256] = {0};
    guint selected_index = gtk_drop_down_get_selected(ui->admin_role);
    (void)button;
    if (!admin_role_from_index(selected_index, &role) ||
        !prs_auth_create_user(ui->database, &ui->session,
                              gtk_editable_get_text(GTK_EDITABLE(ui->admin_username)),
                              gtk_editable_get_text(GTK_EDITABLE(ui->admin_password)), role,
                              error, sizeof(error))) {
        gtk_label_set_text(ui->admin_status,
                           error[0] != '\0' ? error : "Select a valid role for the new account.");
        return;
    }
    set_entry(ui->admin_username, "");
    gtk_editable_set_text(GTK_EDITABLE(ui->admin_password), "");
    gtk_drop_down_set_selected(ui->admin_role, 0U);
    gtk_label_set_text(ui->admin_status, "User account created.");
}

static void on_create_provider(GtkButton *button, gpointer data) {
    PrsUi *ui = data;
    long long provider_id = 0;
    char error[256] = {0};
    char message[100];
    (void)button;
    if (!prs_provider_create(ui->database, &ui->session,
                             gtk_editable_get_text(GTK_EDITABLE(ui->admin_provider_name)),
                             gtk_editable_get_text(GTK_EDITABLE(ui->admin_specialty)),
                             &provider_id, error, sizeof(error))) {
        gtk_label_set_text(ui->admin_status, error);
        return;
    }
    set_entry(ui->admin_provider_name, ""); set_entry(ui->admin_specialty, "");
    (void)g_snprintf(message, sizeof(message), "Provider created with ID %lld.", provider_id);
    gtk_label_set_text(ui->admin_status, message);
}

static void on_create_backup(GtkButton *button, gpointer data) {
    PrsUi *ui = data;
    char backup_path[PRS_PATH_CAPACITY] = {0};
    char error[256] = {0};
    char message[PRS_PATH_CAPACITY + 32U];
    (void)button;
    if (!prs_backup_create_timestamped(
            ui->database, &ui->session,
            gtk_editable_get_text(GTK_EDITABLE(ui->admin_backup_directory)), backup_path,
            sizeof(backup_path), error, sizeof(error))) {
        gtk_label_set_text(ui->admin_status, error);
        return;
    }
    (void)g_snprintf(message, sizeof(message), "Backup created: %s", backup_path);
    gtk_label_set_text(ui->admin_status, message);
}

static void on_restore_backup(GtkButton *button, gpointer data) {
    PrsUi *ui = data;
    char error[256] = {0};
    (void)button;
    if (!prs_backup_restore(ui->database, &ui->session,
                            gtk_editable_get_text(GTK_EDITABLE(ui->admin_restore_path)),
                            error, sizeof(error))) {
        gtk_label_set_text(ui->admin_status, error);
        return;
    }
    gtk_label_set_text(ui->admin_status,
                       "Backup restored. Sign out and sign in to refresh your session.");
}

static GtkWidget *build_login(PrsUi *ui) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *title = gtk_label_new("Patient Registration System");
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *grid = gtk_grid_new();
    GtkWidget *sign_in = gtk_button_new_with_label("Sign in");
    GtkWidget *bootstrap_grid = gtk_grid_new();
    bool initial_setup_needed = !has_admin_account(ui);
    ui->username = GTK_ENTRY(gtk_entry_new());
    ui->password = GTK_PASSWORD_ENTRY(gtk_password_entry_new());
    ui->bootstrap_username = GTK_ENTRY(gtk_entry_new());
    ui->bootstrap_password = GTK_PASSWORD_ENTRY(gtk_password_entry_new());
    ui->bootstrap_confirm = GTK_PASSWORD_ENTRY(gtk_password_entry_new());
    ui->bootstrap_admin = GTK_BUTTON(gtk_button_new_with_label("Create first administrator"));
    ui->login_status = GTK_LABEL(gtk_label_new(""));
    ui->bootstrap_status = GTK_LABEL(gtk_label_new(""));
    gtk_widget_set_margin_top(page, 72); gtk_widget_set_margin_start(page, 48);
    gtk_widget_set_margin_end(page, 48); gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(page, "page-shell");
    gtk_widget_add_css_class(title, "title-1"); gtk_widget_set_halign(card, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(card, 480, -1); gtk_widget_add_css_class(card, "card");
    gtk_widget_add_css_class(sign_in, "primary-button");
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12); gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(bootstrap_grid), 10); gtk_grid_set_column_spacing(GTK_GRID(bootstrap_grid), 12);
    gtk_entry_set_placeholder_text(ui->username, "Username");
    gtk_entry_set_placeholder_text(ui->bootstrap_username, "Choose username");
    add_row(GTK_GRID(grid), 0, "Username", GTK_WIDGET(ui->username));
    add_row(GTK_GRID(grid), 1, "Password", GTK_WIDGET(ui->password));
    add_row(GTK_GRID(bootstrap_grid), 0, "Administrator username", GTK_WIDGET(ui->bootstrap_username));
    add_row(GTK_GRID(bootstrap_grid), 1, "Password", GTK_WIDGET(ui->bootstrap_password));
    add_row(GTK_GRID(bootstrap_grid), 2, "Confirm password", GTK_WIDGET(ui->bootstrap_confirm));
    gtk_label_set_xalign(ui->login_status, 0.0F); gtk_label_set_wrap(ui->login_status, TRUE);
    gtk_label_set_xalign(ui->bootstrap_status, 0.0F); gtk_label_set_wrap(ui->bootstrap_status, TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(ui->login_status), "error");
    gtk_widget_add_css_class(GTK_WIDGET(ui->bootstrap_status), "error");
    gtk_widget_set_visible(GTK_WIDGET(ui->bootstrap_username), initial_setup_needed);
    gtk_widget_set_visible(GTK_WIDGET(ui->bootstrap_password), initial_setup_needed);
    gtk_widget_set_visible(GTK_WIDGET(ui->bootstrap_confirm), initial_setup_needed);
    gtk_widget_set_visible(GTK_WIDGET(ui->bootstrap_admin), initial_setup_needed);
    gtk_widget_set_visible(GTK_WIDGET(ui->bootstrap_status), initial_setup_needed);
    g_signal_connect(sign_in, "clicked", G_CALLBACK(on_sign_in), ui);
    g_signal_connect(ui->bootstrap_admin, "clicked", G_CALLBACK(on_bootstrap_admin), ui);
    gtk_box_append(GTK_BOX(card), grid); gtk_box_append(GTK_BOX(card), sign_in);
    if (initial_setup_needed) {
        gtk_box_append(GTK_BOX(card), gtk_label_new("Create the first administrator account"));
        gtk_box_append(GTK_BOX(card), bootstrap_grid);
        gtk_box_append(GTK_BOX(card), GTK_WIDGET(ui->bootstrap_admin));
        gtk_box_append(GTK_BOX(card), GTK_WIDGET(ui->bootstrap_status));
    }
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(ui->login_status));
    gtk_box_append(GTK_BOX(page), title); gtk_box_append(GTK_BOX(page), card);
    return page;
}

static GtkWidget *action_button(PrsUi *ui, const char *label, GCallback callback) {
    GtkWidget *button = gtk_button_new_with_label(label);
    gtk_widget_set_hexpand(button, TRUE); gtk_widget_set_size_request(button, 230, 84);
    g_signal_connect(button, "clicked", callback, ui);
    return button;
}

static GtkWidget *build_dashboard(PrsUi *ui) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *title = gtk_label_new("Dashboard");
    GtkWidget *actions = gtk_grid_new();
    GtkWidget *sign_out = gtk_button_new_with_label("Sign out");
    ui->dashboard_status = GTK_LABEL(gtk_label_new("Sign in to begin."));
    gtk_widget_set_margin_top(page, 36); gtk_widget_set_margin_start(page, 48);
    gtk_widget_set_margin_end(page, 48); gtk_widget_add_css_class(page, "page-shell");
    gtk_widget_add_css_class(title, "title-1");
    gtk_widget_add_css_class(GTK_WIDGET(sign_out), "secondary-button");
    gtk_widget_set_halign(title, GTK_ALIGN_START); gtk_label_set_xalign(ui->dashboard_status, 0.0F);
    gtk_grid_set_row_spacing(GTK_GRID(actions), 12); gtk_grid_set_column_spacing(GTK_GRID(actions), 12);
    gtk_grid_attach(GTK_GRID(actions), action_button(ui, "Register patient", G_CALLBACK(on_register)), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(actions), action_button(ui, "Search patients", G_CALLBACK(on_open_search)), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(actions), action_button(ui, "Medical history", G_CALLBACK(on_open_medical)), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(actions), action_button(ui, "Appointments", G_CALLBACK(on_open_appointments)), 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(actions), action_button(ui, "Administration", G_CALLBACK(on_open_administration)), 0, 2, 1, 1);
    g_signal_connect(sign_out, "clicked", G_CALLBACK(on_sign_out), ui);
    gtk_box_append(GTK_BOX(page), title); gtk_box_append(GTK_BOX(page), GTK_WIDGET(ui->dashboard_status));
    gtk_box_append(GTK_BOX(page), actions); gtk_box_append(GTK_BOX(page), sign_out);
    return page;
}

static GtkWidget *build_patient_form(PrsUi *ui) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *back = gtk_button_new_with_label("Back to dashboard");
    GtkWidget *scroll = gtk_scrolled_window_new();
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *grid = gtk_grid_new();
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    PrsPatientForm *form = g_new0(PrsPatientForm, 1);
    form->ui = ui; form->title = GTK_LABEL(gtk_label_new("Register new patient"));
    form->status = GTK_LABEL(gtk_label_new(""));
    form->first_name = GTK_ENTRY(gtk_entry_new()); form->last_name = GTK_ENTRY(gtk_entry_new());
    form->date_of_birth = GTK_ENTRY(gtk_entry_new()); form->sex = GTK_ENTRY(gtk_entry_new());
    form->phone = GTK_ENTRY(gtk_entry_new()); form->email = GTK_ENTRY(gtk_entry_new());
    form->clinic_identifier = GTK_ENTRY(gtk_entry_new()); form->insurance_reference = GTK_ENTRY(gtk_entry_new());
    form->address = GTK_ENTRY(gtk_entry_new()); form->emergency_name = GTK_ENTRY(gtk_entry_new());
    form->emergency_phone = GTK_ENTRY(gtk_entry_new()); form->guardian_name = GTK_ENTRY(gtk_entry_new());
    form->guardian_phone = GTK_ENTRY(gtk_entry_new()); form->patient_notes = GTK_ENTRY(gtk_entry_new());
    form->duplicate_acknowledgement = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("I reviewed possible duplicates and confirm this is a new patient."));
    form->save = GTK_BUTTON(gtk_button_new_with_label("Save patient")); ui->patient_form = form;
    gtk_widget_set_margin_top(page, 24); gtk_widget_set_margin_bottom(page, 24);
    gtk_widget_set_margin_start(page, 36); gtk_widget_set_margin_end(page, 36);
    gtk_widget_add_css_class(page, "page-shell");
    gtk_widget_add_css_class(GTK_WIDGET(form->title), "title-2"); gtk_label_set_xalign(form->status, 0.0F);
    gtk_widget_add_css_class(GTK_WIDGET(form->save), "primary-button");
    gtk_widget_add_css_class(cancel, "secondary-button");
    gtk_label_set_wrap(form->status, TRUE); gtk_widget_add_css_class(GTK_WIDGET(form->status), "error");
    gtk_widget_set_hexpand(GTK_WIDGET(grid), TRUE); gtk_widget_set_vexpand(GTK_WIDGET(grid), TRUE);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10); gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_entry_set_placeholder_text(form->date_of_birth, "YYYY-MM-DD");
    gtk_entry_set_placeholder_text(form->sex, "Required selection/value");
    gtk_entry_set_placeholder_text(form->email, "Optional");
    add_row(GTK_GRID(grid), 0, "First name *", GTK_WIDGET(form->first_name));
    add_row(GTK_GRID(grid), 1, "Last name *", GTK_WIDGET(form->last_name));
    add_row(GTK_GRID(grid), 2, "Date of birth *", GTK_WIDGET(form->date_of_birth));
    add_row(GTK_GRID(grid), 3, "Sex at registration *", GTK_WIDGET(form->sex));
    add_row(GTK_GRID(grid), 4, "Phone *", GTK_WIDGET(form->phone));
    add_row(GTK_GRID(grid), 5, "Email", GTK_WIDGET(form->email));
    add_row(GTK_GRID(grid), 6, "Clinic identifier", GTK_WIDGET(form->clinic_identifier));
    add_row(GTK_GRID(grid), 7, "Insurance reference", GTK_WIDGET(form->insurance_reference));
    add_row(GTK_GRID(grid), 8, "Address *", GTK_WIDGET(form->address));
    add_row(GTK_GRID(grid), 9, "Emergency contact name *", GTK_WIDGET(form->emergency_name));
    add_row(GTK_GRID(grid), 10, "Emergency contact phone *", GTK_WIDGET(form->emergency_phone));
    add_row(GTK_GRID(grid), 11, "Guardian name", GTK_WIDGET(form->guardian_name));
    add_row(GTK_GRID(grid), 12, "Guardian phone", GTK_WIDGET(form->guardian_phone));
    add_row(GTK_GRID(grid), 13, "Patient notes", GTK_WIDGET(form->patient_notes));
    gtk_widget_set_visible(GTK_WIDGET(form->duplicate_acknowledgement), FALSE);
    g_signal_connect(back, "clicked", G_CALLBACK(on_dashboard), ui);
    g_signal_connect(cancel, "clicked", G_CALLBACK(on_form_cancel), form);
    g_signal_connect(form->save, "clicked", G_CALLBACK(on_save_patient), form);
    gtk_widget_set_halign(GTK_WIDGET(actions), GTK_ALIGN_END);
    gtk_widget_set_hexpand(GTK_WIDGET(content), TRUE);
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(form->title)); gtk_box_append(GTK_BOX(content), GTK_WIDGET(form->status));
    gtk_box_append(GTK_BOX(content), grid); gtk_box_append(GTK_BOX(content), GTK_WIDGET(form->duplicate_acknowledgement));
    gtk_box_append(GTK_BOX(actions), cancel); gtk_box_append(GTK_BOX(actions), GTK_WIDGET(form->save)); gtk_box_append(GTK_BOX(content), actions);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), content);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_append(GTK_BOX(page), back); gtk_box_append(GTK_BOX(page), scroll);
    return page;
}

static GtkWidget *build_search(PrsUi *ui) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *back = gtk_button_new_with_label("Back to dashboard");
    GtkWidget *title = gtk_label_new("Find patient");
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *search = gtk_button_new_with_label("Search");
    GtkWidget *scroll = gtk_scrolled_window_new();
    ui->search_entry = GTK_ENTRY(gtk_entry_new());
    ui->search_status = GTK_LABEL(gtk_label_new("Search by patient ID, full name, phone number, or date of birth."));
    ui->search_results = GTK_LIST_BOX(gtk_list_box_new());
    gtk_widget_set_margin_top(page, 24); gtk_widget_set_margin_start(page, 36); gtk_widget_set_margin_end(page, 36);
    gtk_widget_add_css_class(page, "page-shell");
    gtk_widget_add_css_class(title, "title-2"); gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_widget_set_hexpand(GTK_WIDGET(ui->search_entry), TRUE); gtk_label_set_xalign(ui->search_status, 0.0F);
    gtk_widget_set_hexpand(GTK_WIDGET(ui->search_results), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(ui->search_results), TRUE);
    gtk_list_box_set_selection_mode(ui->search_results, GTK_SELECTION_NONE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(ui->search_results));
    g_signal_connect(back, "clicked", G_CALLBACK(on_dashboard), ui); g_signal_connect(search, "clicked", G_CALLBACK(on_search), ui);
    g_signal_connect(ui->search_results, "row-activated", G_CALLBACK(on_search_row), ui);
    gtk_box_append(GTK_BOX(bar), GTK_WIDGET(ui->search_entry)); gtk_box_append(GTK_BOX(bar), search);
    gtk_box_append(GTK_BOX(page), back); gtk_box_append(GTK_BOX(page), title); gtk_box_append(GTK_BOX(page), bar);
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(ui->search_status)); gtk_box_append(GTK_BOX(page), scroll);
    return page;
}

static GtkWidget *build_medical_history(PrsUi *ui) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *back = gtk_button_new_with_label("Back to patient profile");
    GtkWidget *title = gtk_label_new("Medical history");
    GtkWidget *current_title = gtk_label_new("Current history");
    GtkWidget *grid = gtk_grid_new();
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    ui->history_save = GTK_BUTTON(gtk_button_new_with_label("Add history entry"));
    ui->history_delete = GTK_BUTTON(gtk_button_new_with_label("Delete history"));
    ui->history_current = GTK_LABEL(gtk_label_new("Not recorded."));
    ui->history_status = GTK_LABEL(gtk_label_new(""));
    ui->history_blood_group = GTK_ENTRY(gtk_entry_new());
    ui->history_allergies = GTK_ENTRY(gtk_entry_new());
    ui->history_conditions = GTK_ENTRY(gtk_entry_new());
    ui->history_medications = GTK_ENTRY(gtk_entry_new());
    ui->history_surgeries = GTK_ENTRY(gtk_entry_new());
    ui->history_notes = GTK_ENTRY(gtk_entry_new());
    gtk_widget_set_margin_top(page, 24); gtk_widget_set_margin_start(page, 36);
    gtk_widget_set_margin_end(page, 36); gtk_widget_add_css_class(title, "title-2");
    gtk_label_set_xalign(ui->history_current, 0.0F); gtk_label_set_wrap(ui->history_current, TRUE);
    gtk_label_set_xalign(ui->history_status, 0.0F); gtk_label_set_wrap(ui->history_status, TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(ui->history_status), "error");
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10); gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_entry_set_placeholder_text(ui->history_blood_group, "A+, A-, B+, B-, AB+, AB-, O+, O-, or UNKNOWN");
    add_row(GTK_GRID(grid), 0, "Blood group", GTK_WIDGET(ui->history_blood_group));
    add_row(GTK_GRID(grid), 1, "Allergies", GTK_WIDGET(ui->history_allergies));
    add_row(GTK_GRID(grid), 2, "Chronic conditions", GTK_WIDGET(ui->history_conditions));
    add_row(GTK_GRID(grid), 3, "Current medications", GTK_WIDGET(ui->history_medications));
    add_row(GTK_GRID(grid), 4, "Prior surgeries", GTK_WIDGET(ui->history_surgeries));
    add_row(GTK_GRID(grid), 5, "Clinical notes", GTK_WIDGET(ui->history_notes));
    g_signal_connect(back, "clicked", G_CALLBACK(on_back_to_profile), ui);
    g_signal_connect(ui->history_save, "clicked", G_CALLBACK(on_save_medical_history), ui);
    g_signal_connect(ui->history_delete, "clicked", G_CALLBACK(on_delete_medical_history), ui);
    gtk_widget_set_sensitive(GTK_WIDGET(ui->history_delete), FALSE);
    gtk_box_append(GTK_BOX(actions), GTK_WIDGET(ui->history_save));
    gtk_box_append(GTK_BOX(actions), GTK_WIDGET(ui->history_delete));
    gtk_box_append(GTK_BOX(page), back); gtk_box_append(GTK_BOX(page), title);
    gtk_box_append(GTK_BOX(page), current_title); gtk_box_append(GTK_BOX(page), GTK_WIDGET(ui->history_current));
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(ui->history_status)); gtk_box_append(GTK_BOX(page), grid);
    gtk_box_append(GTK_BOX(page), actions);
    return page;
}

static GtkWidget *build_appointments(PrsUi *ui) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *back = gtk_button_new_with_label("Back to patient profile");
    GtkWidget *title = gtk_label_new("Appointments");
    GtkWidget *schedule_grid = gtk_grid_new();
    GtkWidget *cancel_grid = gtk_grid_new();
    GtkWidget *create = gtk_button_new_with_label("Schedule appointment");
    GtkWidget *cancel = gtk_button_new_with_label("Cancel appointment");
    ui->appointment_list = GTK_LABEL(gtk_label_new("No appointments recorded."));
    ui->appointment_status = GTK_LABEL(gtk_label_new(""));
    ui->appointment_provider_id = GTK_ENTRY(gtk_entry_new());
    ui->appointment_start = GTK_ENTRY(gtk_entry_new());
    ui->appointment_end = GTK_ENTRY(gtk_entry_new());
    ui->appointment_reason = GTK_ENTRY(gtk_entry_new());
    ui->appointment_cancel_id = GTK_ENTRY(gtk_entry_new());
    ui->appointment_cancel_reason = GTK_ENTRY(gtk_entry_new());
    gtk_widget_set_margin_top(page, 24); gtk_widget_set_margin_start(page, 36);
    gtk_widget_set_margin_end(page, 36); gtk_widget_add_css_class(title, "title-2");
    gtk_label_set_xalign(ui->appointment_list, 0.0F); gtk_label_set_wrap(ui->appointment_list, TRUE);
    gtk_label_set_xalign(ui->appointment_status, 0.0F); gtk_label_set_wrap(ui->appointment_status, TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(ui->appointment_status), "error");
    gtk_grid_set_row_spacing(GTK_GRID(schedule_grid), 10); gtk_grid_set_column_spacing(GTK_GRID(schedule_grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(cancel_grid), 10); gtk_grid_set_column_spacing(GTK_GRID(cancel_grid), 12);
    gtk_entry_set_placeholder_text(ui->appointment_provider_id, "Provider ID");
    gtk_entry_set_placeholder_text(ui->appointment_start, "YYYY-MM-DD HH:MM");
    gtk_entry_set_placeholder_text(ui->appointment_end, "YYYY-MM-DD HH:MM");
    add_row(GTK_GRID(schedule_grid), 0, "Provider ID *", GTK_WIDGET(ui->appointment_provider_id));
    add_row(GTK_GRID(schedule_grid), 1, "Start *", GTK_WIDGET(ui->appointment_start));
    add_row(GTK_GRID(schedule_grid), 2, "End *", GTK_WIDGET(ui->appointment_end));
    add_row(GTK_GRID(schedule_grid), 3, "Reason *", GTK_WIDGET(ui->appointment_reason));
    add_row(GTK_GRID(cancel_grid), 0, "Appointment ID *", GTK_WIDGET(ui->appointment_cancel_id));
    add_row(GTK_GRID(cancel_grid), 1, "Cancellation reason *", GTK_WIDGET(ui->appointment_cancel_reason));
    g_signal_connect(back, "clicked", G_CALLBACK(on_back_to_profile), ui);
    g_signal_connect(create, "clicked", G_CALLBACK(on_create_appointment), ui);
    g_signal_connect(cancel, "clicked", G_CALLBACK(on_cancel_appointment), ui);
    gtk_box_append(GTK_BOX(page), back); gtk_box_append(GTK_BOX(page), title);
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(ui->appointment_list));
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(ui->appointment_status));
    gtk_box_append(GTK_BOX(page), schedule_grid); gtk_box_append(GTK_BOX(page), create);
    gtk_box_append(GTK_BOX(page), cancel_grid); gtk_box_append(GTK_BOX(page), cancel);
    return page;
}

static GtkWidget *build_administration(PrsUi *ui) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *back = gtk_button_new_with_label("Back to dashboard");
    GtkWidget *title = gtk_label_new("Administration");
    GtkWidget *user_grid = gtk_grid_new();
    GtkWidget *provider_grid = gtk_grid_new();
    GtkWidget *backup_grid = gtk_grid_new();
    GtkWidget *create_user = gtk_button_new_with_label("Create user");
    GtkWidget *create_provider = gtk_button_new_with_label("Create provider");
    GtkWidget *create_backup = gtk_button_new_with_label("Create backup");
    GtkWidget *restore_backup = gtk_button_new_with_label("Restore validated backup");
    GtkStringList *role_list = gtk_string_list_new(NULL);

    ui->admin_status = GTK_LABEL(gtk_label_new(""));
    ui->admin_username = GTK_ENTRY(gtk_entry_new());
    ui->admin_password = GTK_PASSWORD_ENTRY(gtk_password_entry_new());
    ui->admin_role = GTK_DROP_DOWN(gtk_drop_down_new(G_LIST_MODEL(role_list), NULL));
    gtk_string_list_append(role_list, "Receptionist");
    gtk_string_list_append(role_list, "Clinician");
    gtk_string_list_append(role_list, "Administrator");
    gtk_drop_down_set_selected(ui->admin_role, 0U);
    ui->admin_provider_name = GTK_ENTRY(gtk_entry_new());
    ui->admin_specialty = GTK_ENTRY(gtk_entry_new());
    ui->admin_backup_directory = GTK_ENTRY(gtk_entry_new());
    ui->admin_restore_path = GTK_ENTRY(gtk_entry_new());
    set_entry(ui->admin_backup_directory, "backups");
    gtk_widget_set_margin_top(page, 24); gtk_widget_set_margin_start(page, 36);
    gtk_widget_set_margin_end(page, 36); gtk_widget_add_css_class(title, "title-2");
    gtk_label_set_xalign(ui->admin_status, 0.0F); gtk_label_set_wrap(ui->admin_status, TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(ui->admin_status), "error");
    gtk_grid_set_row_spacing(GTK_GRID(user_grid), 10); gtk_grid_set_column_spacing(GTK_GRID(user_grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(provider_grid), 10); gtk_grid_set_column_spacing(GTK_GRID(provider_grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(backup_grid), 10); gtk_grid_set_column_spacing(GTK_GRID(backup_grid), 12);
    add_row(GTK_GRID(user_grid), 0, "New username", GTK_WIDGET(ui->admin_username));
    add_row(GTK_GRID(user_grid), 1, "Initial password", GTK_WIDGET(ui->admin_password));
    add_row(GTK_GRID(user_grid), 2, "Role", GTK_WIDGET(ui->admin_role));
    add_row(GTK_GRID(provider_grid), 0, "Provider name", GTK_WIDGET(ui->admin_provider_name));
    add_row(GTK_GRID(provider_grid), 1, "Specialty", GTK_WIDGET(ui->admin_specialty));
    add_row(GTK_GRID(backup_grid), 0, "Backup directory", GTK_WIDGET(ui->admin_backup_directory));
    add_row(GTK_GRID(backup_grid), 1, "Backup file to restore", GTK_WIDGET(ui->admin_restore_path));
    g_signal_connect(back, "clicked", G_CALLBACK(on_dashboard), ui);
    g_signal_connect(create_user, "clicked", G_CALLBACK(on_create_user), ui);
    g_signal_connect(create_provider, "clicked", G_CALLBACK(on_create_provider), ui);
    g_signal_connect(create_backup, "clicked", G_CALLBACK(on_create_backup), ui);
    g_signal_connect(restore_backup, "clicked", G_CALLBACK(on_restore_backup), ui);
    gtk_box_append(GTK_BOX(page), back); gtk_box_append(GTK_BOX(page), title);
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(ui->admin_status));
    gtk_box_append(GTK_BOX(page), gtk_label_new("User accounts"));
    gtk_box_append(GTK_BOX(page), user_grid); gtk_box_append(GTK_BOX(page), create_user);
    gtk_box_append(GTK_BOX(page), gtk_label_new("Providers"));
    gtk_box_append(GTK_BOX(page), provider_grid); gtk_box_append(GTK_BOX(page), create_provider);
    gtk_box_append(GTK_BOX(page), gtk_label_new("Protected database backups"));
    gtk_box_append(GTK_BOX(page), backup_grid); gtk_box_append(GTK_BOX(page), create_backup);
    gtk_box_append(GTK_BOX(page), restore_backup);
    return page;
}

static GtkWidget *build_profile(PrsUi *ui) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *back = gtk_button_new_with_label("Back to search");
    GtkWidget *title = gtk_label_new("Patient profile");
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *medical = gtk_button_new_with_label("Medical history");
    GtkWidget *appointments = gtk_button_new_with_label("Appointments");
    ui->profile_details = GTK_LABEL(gtk_label_new("")); ui->profile_status = GTK_LABEL(gtk_label_new(""));
    ui->profile_edit = GTK_BUTTON(gtk_button_new_with_label("Edit details"));
    ui->profile_status_change = GTK_BUTTON(gtk_button_new_with_label("Deactivate record"));
    gtk_widget_set_margin_top(page, 24); gtk_widget_set_margin_start(page, 36); gtk_widget_set_margin_end(page, 36);
    gtk_widget_add_css_class(page, "page-shell");
    gtk_widget_add_css_class(title, "title-2"); gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_widget_add_css_class(GTK_WIDGET(ui->profile_details), "profile-details-box");
    gtk_label_set_xalign(ui->profile_details, 0.0F); gtk_label_set_yalign(ui->profile_details, 0.0F); gtk_label_set_wrap(ui->profile_details, TRUE);
    gtk_label_set_xalign(ui->profile_status, 0.0F); gtk_label_set_wrap(ui->profile_status, TRUE);
    g_signal_connect(back, "clicked", G_CALLBACK(on_open_search), ui); g_signal_connect(ui->profile_edit, "clicked", G_CALLBACK(on_edit_profile), ui);
    g_signal_connect(ui->profile_status_change, "clicked", G_CALLBACK(on_change_patient_status), ui);
    g_signal_connect(medical, "clicked", G_CALLBACK(on_open_medical), ui); g_signal_connect(appointments, "clicked", G_CALLBACK(on_open_appointments), ui);
    gtk_box_append(GTK_BOX(actions), GTK_WIDGET(ui->profile_edit)); gtk_box_append(GTK_BOX(actions), GTK_WIDGET(ui->profile_status_change)); gtk_box_append(GTK_BOX(actions), medical); gtk_box_append(GTK_BOX(actions), appointments);
    gtk_box_append(GTK_BOX(page), back); gtk_box_append(GTK_BOX(page), title); gtk_box_append(GTK_BOX(page), GTK_WIDGET(ui->profile_details));
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(ui->profile_status)); gtk_box_append(GTK_BOX(page), actions);
    return page;
}

static void on_activate(GtkApplication *application, gpointer data) {
    PrsUi *ui = data;
    GtkWidget *window = gtk_application_window_new(application);
    GtkWidget *header = gtk_header_bar_new();
    gtk_window_set_title(GTK_WINDOW(window), "Patient Registration System");
    gtk_window_set_default_size(GTK_WINDOW(window), 1100, 820);
    gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
    gtk_widget_add_css_class(window, "industrial-window");
    apply_global_css();
    gtk_window_set_titlebar(GTK_WINDOW(window), header);
    ui->stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_transition_type(ui->stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_add_named(ui->stack, build_login(ui), "login");
    gtk_stack_add_named(ui->stack, build_dashboard(ui), "dashboard");
    gtk_stack_add_named(ui->stack, build_patient_form(ui), "patient-form");
    gtk_stack_add_named(ui->stack, build_search(ui), "search");
    gtk_stack_add_named(ui->stack, build_profile(ui), "profile");
    gtk_stack_add_named(ui->stack, build_medical_history(ui), "medical-history");
    gtk_stack_add_named(ui->stack, build_appointments(ui), "appointments");
    gtk_stack_add_named(ui->stack, build_administration(ui), "administration");
    show(ui, "login"); gtk_window_set_child(GTK_WINDOW(window), GTK_WIDGET(ui->stack));
    gtk_window_present(GTK_WINDOW(window));
}

int prs_ui_run(int argc, char **argv, PrsDatabase *database) {
    GtkApplication *application = gtk_application_new("org.prs.patientregistration", G_APPLICATION_DEFAULT_FLAGS);
    PrsUi *ui = g_new0(PrsUi, 1);
    int status;
    ui->database = database;
    ui->last_activity_usec = g_get_monotonic_time();
    g_signal_connect(application, "activate", G_CALLBACK(on_activate), ui);
    (void)g_timeout_add_seconds(30, on_session_timer, ui);
    status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application); g_free(ui->patient_form); g_free(ui);
    return status;
}
