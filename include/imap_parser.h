#ifndef AMIGMAIL_IMAP_PARSER_H
#define AMIGMAIL_IMAP_PARSER_H

#include "buffer.h"

typedef enum AmgImapEventType {
    AMG_IMAP_EVENT_NONE = 0,
    AMG_IMAP_EVENT_LINE,
    AMG_IMAP_EVENT_LITERAL,
    AMG_IMAP_EVENT_ERROR
} AmgImapEventType;

typedef enum AmgImapParserFailure {
    AMG_IMAP_PARSER_FAILURE_NONE = 0,
    AMG_IMAP_PARSER_FAILURE_LINE_LIMIT,
    AMG_IMAP_PARSER_FAILURE_LITERAL_LIMIT,
    AMG_IMAP_PARSER_FAILURE_BUFFER_LIMIT
} AmgImapParserFailure;

typedef struct AmgImapEvent {
    AmgImapEventType type;
    const unsigned char *data;
    size_t length;
} AmgImapEvent;

typedef struct AmgImapParser {
    AmgBuffer pending;
    AmgBuffer event_data;
    size_t literal_remaining;
    size_t failure_size;
    size_t failure_limit;
    AmgImapParserFailure failure;
    int waiting_literal;
    int failed;
} AmgImapParser;

typedef struct AmgImapFetchRecord {
    unsigned long uid;
    unsigned long rfc822_size;
    const unsigned char *literal;
    size_t literal_length;
    int seen;
    int flagged;
    int deleted;
} AmgImapFetchRecord;

void amg_imap_parser_init(AmgImapParser *parser);
void amg_imap_parser_free(AmgImapParser *parser);
int amg_imap_parser_feed(AmgImapParser *parser, const void *data, size_t length);
int amg_imap_parser_next(AmgImapParser *parser, AmgImapEvent *event);
int amg_imap_parse_literal_length(const unsigned char *line, size_t length, size_t *literal_length);
int amg_imap_greeting_is_success(const unsigned char *line, size_t length);
int amg_imap_greeting_is_preauth(const unsigned char *line, size_t length);
int amg_imap_greeting_status(const unsigned char *data, size_t length);
int amg_imap_parse_exists(const unsigned char *data, size_t length,
                          unsigned long *exists);
int amg_imap_parse_uidvalidity(const unsigned char *data, size_t length,
                               unsigned long *uid_validity);
int amg_imap_parse_fetch_sequence(const unsigned char *data, size_t length,
                                  unsigned long uid,
                                  unsigned long *sequence);
int amg_imap_fetch_record_next(const unsigned char *data, size_t length,
                               size_t *position,
                               AmgImapFetchRecord *record);

#endif
