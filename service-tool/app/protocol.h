/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Execution of an -M- protocol document (SERVICE-TOOL F3).
 *
 * The panel performs a procedure it did not author (ADR-0030 d8): the document
 * is parsed as carried and its steps run in order, each step's precondition
 * being the previous step's pass, as ADR-0028 d1 orders commissioning.
 *
 * There is deliberately NO mutator for a step's text, criterion or order, and
 * none for a result already recorded. S11 is a property of this interface, not
 * a rule the UI is trusted to follow: a caller cannot compose, edit, reorder or
 * skip because the API offers no way to. What it offers is one call that
 * records an outcome for the step that is next, and refuses with a reason
 * otherwise (S10 -- a refusal is presented, never silent).
 *
 * What the panel records here is an operator's judgement. Within a step it may
 * measure and propose; the filed record determines (ADR-0030 d9).
 */
#ifndef IGROW_PROTOCOL_H
#define IGROW_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROTO_MAX_STEPS 64
#define PROTO_SECTION_MAX 56
#define PROTO_TEXT_MAX 120
#define PROTO_CRIT_MAX 180

typedef enum {
    PROTO_PENDING = 0,
    PROTO_PASSED,
    PROTO_FAILED,
} proto_state_t;

typedef struct {
    uint16_t      number; /* the step number the document carries, not our index */
    proto_state_t state;
    char          section[PROTO_SECTION_MAX];
    char          text[PROTO_TEXT_MAX];
    char          criterion[PROTO_CRIT_MAX];
} proto_step_t;

typedef struct {
    char         instance[32]; /* Exxxx-VVVVVV-NNNNNN, given to the run, never derived */
    char         identity[72]; /* the document's object key: Exxxx-VVVVVV-M-<slug> */
    proto_step_t steps[PROTO_MAX_STEPS];
    uint16_t     count;
    uint16_t     cursor;   /* index of the only step that may run next */
    bool         loaded;
    bool         finished; /* every step has an outcome, or one failed */
} protocol_t;

/* Parse a carried document. Returns false if it holds no step table. */
bool protocol_load(const char *doc, size_t len, const char *identity);

/* Load the document this build carries. */
bool protocol_load_embedded(void);

const protocol_t *protocol_get(void);

/* Record an outcome for step `index`. Refuses, with a reason in `why`, when the
 * step is not the one due, when it already has an outcome, or when the run is
 * over. `why` is a static string and is always set on refusal. */
bool protocol_record(uint16_t index, bool passed, const char **why);

/* The step due next, or NULL when the run is over or nothing is loaded. */
const proto_step_t *protocol_current(void);

/* Start the run again from step one. Discards outcomes; the document is
 * re-parsed from the carried bytes so a run can never inherit an edit. */
void protocol_restart(void);

/* Counts for presentation. */
uint16_t protocol_passed_count(void);
uint16_t protocol_failed_count(void);

/* The instance the run is performed against. An input, fixed before step one:
 * serials are issued by the ERP and never by a client (ADR-0021 d4,
 * ADR-0022 d4), so the tool takes one and derives none. Empty until set. */
void        protocol_set_instance(const char *s);
const char *protocol_instance(void);

/* Stream the run's record (SERVICE-TOOL F4). One call per line, no trailing
 * newline; the caller decides where the line goes. Nothing is buffered whole
 * and nothing is kept afterwards (S27) -- the tool composes the record and does
 * not file it, because a tool that filed would need a credential and a network
 * path, and the cabinet must not depend on an accessory holding either.
 *
 * Refuses, with a reason in `why`, while the run is unfinished or no instance
 * has been fixed. */
bool protocol_emit(void (*out)(const char *line), const char **why);

/* The carried document, emitted by tools/mkprotocol.py. */
extern const unsigned char protocol_doc[];
extern const unsigned int  protocol_doc_len;
extern const char          protocol_doc_name[];

#endif /* IGROW_PROTOCOL_H */
