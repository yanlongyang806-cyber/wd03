#ifndef ERRORSTRINGS_H
#define ERRORSTRINGS_H

#define ACCOUNT_HTTP_SUCCESS				"success"							// Success
#define ACCOUNT_HTTP_FAILURE				"failure"
#define ACCOUNT_HTTP_NOT_FOUND				"not_found"							// No records found
#define ACCOUNT_HTTP_NO_ARGS				"no_args"							// Not enough arguments provided

#define ACCOUNT_HTTP_VINDICIA_UNKNOWN		"vindicia_unknown"					// Vindicia has no record of this account

#define ACCOUNT_HTTP_USER_NOT_FOUND			"user_not_found"
#define ACCOUNT_HTTP_INVALID_ACCOUNT_TYPE	"invalid_account_type"
#define ACCOUNT_HTTP_USER_LOGIN_OK			"user_login_ok"
#define ACCOUNT_HTTP_USER_BAD_PASSWORD		"user_bad_password"
#define ACCOUNT_HTTP_USER_NOT_VALIDATED		"user_login_email_not_validated"
#define ACCOUNT_HTTP_USER_FIELDS_MODIFIED	"user_update_ok"
#define ACCOUNT_HTTP_USER_BAD_VALIDATION	"bad_validate_token"
#define ACCOUNT_HTTP_USER_EMAIL_VALIDATED	"user_validate_email_ok"
#define ACCOUNT_HTTP_USER_LOGIN_DISABLED	"user_login_disabled"
#define ACCOUNT_HTTP_USER_LOGIN_DISABLED_LINKED "user_login_disabled_linked"
#define ACCOUNT_HTTP_USER_BANNED            "user_banned"
#define ACCOUNT_HTTP_NEWMACHINEID           "new_machineid"
#define ACCOUNT_HTTP_SAVENEXTBROWSER        "savenextbrowser"
#define ACCOUNT_HTTP_SAVENEXTCLIENT         "savenextclient" // Only used for the Launcher website (ValidateTicketIDEx)
#define ACCOUNT_HTTP_CRYPTIC_LOGIN_DISABLED "cryptic_login_disabled"

#define ACCOUNT_HTTP_PRODUCT_NOT_FOUND		"product_not_found"
#define ACCOUNT_HTTP_PRODUCT_GIVEN			"product_given"
#define ACCOUNT_HTTP_PRODUCT_TAKEN			"product_taken"
#define ACCOUNT_HTTP_PRODUCT_ALREADY_OWNED	"product_already_owned"
#define ACCOUNT_HTTP_PRODUCT_NOT_OWNED		"product_not_owned"
#define ACCOUNT_HTTP_PRODUCT_COULD_NOT_ACTIVATE "product_could_not_activate"

#define ACCOUNT_HTTP_AUTH_FAILED			"auth_failed"
#define ACCOUNT_HTTP_INSUFFICIENT_POINTS	"insufficient_points"
#define ACCOUNT_HTTP_PURCHASE_SUCCESS		"purchase_success"

#define ACCOUNT_HTTP_USER_EXISTS			"user_exists"
#define ACCOUNT_HTTP_EMAIL_EXISTS			"email_exists"
#define ACCOUNT_HTTP_DISPLAYNAME_EXISTS		"displayname_exists"
#define ACCOUNT_HTTP_ALLNAMES_EXISTS		"both_userdisplay_exists"

#define ACCOUNT_HTTP_USER_UNKNOWN_ERROR		"user_error_unknown"

#define ACCOUNT_HTTP_USER_PRODUCT_ACTIVE	"user_product_already_active"
#define ACCOUNT_HTTP_USER_BAD_PRODUCTKEY	"user_invalid_productkey"

#define ACCOUNT_HTTP_TICKET_OK				"ticket_ok"
#define ACCOUNT_HTTP_TICKET_BADSIGNATURE	"bad_ticket_signature"
#define ACCOUNT_HTTP_TICKET_EXPIRED			"ticket_expired"
#define ACCOUNT_HTTP_TICKET_PARSEERR		"ticket_parse_failed"
#define ACCOUNT_HTTP_TICKET_NOT_FOUND		"ticket_not_found"

#define ACCOUNT_HTTP_CONFLICT_TICKET_OK		"conflict_ticket_ok"
#define ACCOUNT_HTTP_CONFLICT_TICKET_NOT_FOUND "conflict_ticket_not_found"

#define ACCOUNT_HTTP_RESTRICTED_ACCOUNTNAME "restricted_user"
#define ACCOUNT_HTTP_INVALID_ACCOUNTNAME	"disallowed_user"
#define ACCOUNT_HTTP_INVALID_DISPLAYNAME	"disallowed_display"
#define ACCOUNT_HTTP_INVALIDLEN_ACCOUNTNAME "disallowed_user_length"
#define ACCOUNT_HTTP_INVALIDLEN_DISPLAYNAME "disallowed_display_length"

#define ACCOUNT_HTTP_KEY_FAILURE			"key_failure"
#define ACCOUNT_HTTP_INVALID_KEY			"invalid_key"
#define ACCOUNT_HTTP_INVALID_RANGE			"invalid_range"
#define ACCOUNT_HTTP_KEY_SUCCESS			"key_set"
#define ACCOUNT_HTTP_KEY_USED				"key_used"

#define ACCOUNT_HTTP_INVALID_CURRENCY		"invalid_currency"
#define ACCOUNT_HTTP_INVALID_IP				"invalid_ip"

#define ACCOUNT_HTTP_INVALID_VID			"invalid_vid"

#define ACCOUNT_HTTP_INVALID_SUBSCRIPTION	"invalid_subscription"
#define ACCOUNT_HTTP_EXISTING_PENDING		"existing_pending_sub"
#define ACCOUNT_HTTP_ALREADY_EXPECTING		"already_expecting"

#define ACCOUNT_HTTP_INVALID_ACTIVATION_KEY "invalid_activation_key"

#define ACCOUNT_HTTP_INVALID_PAYMENT_METHOD "invalid_payment_method"

#define ACCOUNT_HTTP_SECRET_NOT_OK				"secret_not_ok"
#define ACCOUNT_HTTP_SECRET_CORRECT_ANSWERS		"secret_correct_answers"
#define ACCOUNT_HTTP_SECRET_INCORRECT_ANSWERS	"secret_incorrect_answers"

#define ACCOUNT_HTTP_NOT_AUTHORIZED "not_authorized"

#define ACCOUNT_HTTP_INTERNAL_ERROR "internal_error"

#define ACCOUNT_HTTP_INVALID_EMAIL "invalid_email"

#define ACCOUNT_HTTP_EMAIL_ALREADY_RECRUITED "email_already_recruited"

#define ACCOUNT_HTTP_RATE_LIMIT "rate_limit"

#define ACCOUNT_HTTP_UPGRADE_FAILED "upgrade_failed"

#define ACCOUNT_HTTP_MISSING_PRODUCT "missing_required_product"

#define ACCOUNT_HTTP_INVALID_PURCHASE_ID "invalid_purchase_id"

#define ACCOUNT_HTTP_SPENDING_CAP_REACHED "spending_cap_reached"

#define ACCOUNT_HTTP_CURRENCY_LOCKED "currency_locked"

#define ACCOUNT_HTTP_INVALID_STEAMID "invalid_steam_id"
#define ACCOUNT_HTTP_STEAM_FAILURE_RESPONSE "steam_failure_response"

#define ACCOUNT_HTTP_DB_LOCKED "db_locked"

#define ACCOUNT_HTTP_PWUSER_UNKNOWN "pwuser_unknown"
#define ACCOUNT_HTTP_PWUSER_LINKED "pwuser_already_linked"
#define ACCOUNT_HTTP_CRYPTICUSER_LINKED "crypticuser_already_linked"
#define ACCOUNT_HTTP_CRYPTICUSER_NOTLINKED "crypticuser_not_linked"

#define ACCOUNT_HTTP_NO_RESULTS "no_results"

#endif
