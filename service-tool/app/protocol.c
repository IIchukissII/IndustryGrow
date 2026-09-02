/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "protocol.h"

#include <stdio.h>
#include <string.h>

#include "panel_mem.h"

/* The tool's own serial is the board's, not one the ERP issued: the STM32 96-bit
 * unique device ID, read where the part carries it. The record names the tool
 * that performed the run so that two runs of one procedure can be told apart by
 * more than their results. */
#define STM32_UID_BASE 0x1FF1E800U

static void tool_serial(char *dst, size_t cap)
{
    const volatile uint32_t *uid = (const volatile uint32_t *)STM32_UID_BASE;
    (void)snprintf(dst, cap, "%08lX%08lX%08lX", (unsigned long)uid[0], (unsigned long)uid[1],
                   (unsigned long)uid[2]);
}

/* ~26 kB of steps: too much for the internal SRAM the model and the CAN ring
 * already occupy, and it is touched at operator speed, so it goes to SDRAM. */
static protocol_t s_proto PANEL_SDRAM_BSS;

/* --- parsing --------------------------------------------------------------
 *
 * The -M- documents carry their steps as Markdown tables whose header row
 * begins "| # |". The column count varies by table -- bring-up uses
 *
 *     | # | Step | Pass criterion | Result |
 *
 * and the I2C and functional tables add Address or Subject/Stimulus columns --
 * so columns are taken by position from BOTH ends rather than by index: the
 * number is first, the Result the last, the pass criterion the one before it,
 * and everything between is the step's description. That rule holds for every
 * table in every -M- document in the store, and does not need teaching about
 * new ones.
 *
 * Section headings ("## 3. Flash and boot") group the steps for presentation.
 * Step numbers run continuously across sections in these documents, but nothing
 * here relies on that: order is the order the document lists them in.
 */

static void copy_trimmed(char *dst, size_t cap, const char *src, size_t len)
{
    while ((len > 0U) && ((*src == ' ') || (*src == '\t'))) {
        src++;
        len--;
    }
    while ((len > 0U) && ((src[len - 1U] == ' ') || (src[len - 1U] == '\t'))) {
        len--;
    }
    if (len >= cap) {
        len = cap - 1U;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* A line is a table row if it starts with '|'. Splits on '|' and hands back the
 * cell count; cells[] point into the line. */
static unsigned split_row(const char *line, size_t len, const char **cells, size_t *lens,
                          unsigned cap)
{
    if ((len == 0U) || (line[0] != '|')) {
        return 0;
    }
    unsigned n     = 0;
    size_t   start = 1U;
    for (size_t i = 1U; i <= len; i++) {
        if ((i == len) || (line[i] == '|')) {
            if (n < cap) {
                cells[n] = &line[start];
                lens[n]  = i - start;
                n++;
            }
            start = i + 1U;
            if (i == len) {
                break;
            }
        }
    }
    /* A row written "| a | b |" yields a trailing empty cell; drop it. */
    if ((n > 0U) && (lens[n - 1U] == 0U)) {
        n--;
    }
    return n;
}

static bool cell_is_number(const char *s, size_t len, uint16_t *out)
{
    while ((len > 0U) && (*s == ' ')) {
        s++;
        len--;
    }
    if (len == 0U) {
        return false;
    }
    uint16_t v = 0;
    size_t   i = 0;
    for (; i < len; i++) {
        if ((s[i] < '0') || (s[i] > '9')) {
            break;
        }
        v = (uint16_t)((v * 10U) + (uint16_t)(s[i] - '0'));
    }
    if (i == 0U) {
        return false;
    }
    while ((i < len) && (s[i] == ' ')) {
        i++;
    }
    if (i != len) {
        return false; /* trailing junk: not a step number */
    }
    *out = v;
    return true;
}

/* "## 3. Flash and boot" -> "3. Flash and boot" */
static bool section_heading(const char *line, size_t len, char *dst, size_t cap)
{
    if ((len < 4U) || (line[0] != '#') || (line[1] != '#') || (line[2] != ' ')) {
        return false;
    }
    if (line[3] == '#') {
        return false; /* deeper heading, not a section */
    }
    copy_trimmed(dst, cap, &line[3], len - 3U);
    return true;
}

bool protocol_load(const char *doc, size_t len, const char *identity)
{
    /* The instance belongs to the board on the bench, not to the run, so it
     * survives a restart of the run against the same board. */
    char keep[sizeof s_proto.instance];
    memcpy(keep, s_proto.instance, sizeof keep);

    memset(&s_proto, 0, sizeof s_proto);
    memcpy(s_proto.instance, keep, sizeof keep);
    copy_trimmed(s_proto.identity, sizeof s_proto.identity, identity, strlen(identity));

    char     section[PROTO_SECTION_MAX] = "";
    bool     in_table                   = false;
    size_t   pos                        = 0;

    while ((pos < len) && (s_proto.count < PROTO_MAX_STEPS)) {
        size_t eol = pos;
        while ((eol < len) && (doc[eol] != '\n')) {
            eol++;
        }
        size_t llen = eol - pos;
        if ((llen > 0U) && (doc[pos + llen - 1U] == '\r')) {
            llen--;
        }
        const char *line = &doc[pos];

        if (section_heading(line, llen, section, sizeof section)) {
            in_table = false;
        } else if ((llen >= 5U) && (strncmp(line, "| # |", 5) == 0)) {
            in_table = true; /* header row: the rows after the separator are steps */
        } else if (in_table && ((llen == 0U) || (line[0] != '|'))) {
            in_table = false;
        } else if (in_table) {
            const char *cells[8];
            size_t      lens[8];
            const unsigned n = split_row(line, llen, cells, lens, 8U);
            uint16_t       number;
            /* Needs a number, a criterion and a result column at least. */
            if ((n >= 3U) && cell_is_number(cells[0], lens[0], &number)) {
                proto_step_t *st = &s_proto.steps[s_proto.count];
                st->number       = number;
                st->state        = PROTO_PENDING;
                copy_trimmed(st->section, sizeof st->section, section, strlen(section));
                copy_trimmed(st->criterion, sizeof st->criterion, cells[n - 2U], lens[n - 2U]);

                /* Everything between the number and the criterion describes the
                 * step; the middle tables carry two or three such columns. */
                size_t used = 0;
                /* Cells are 0..n-1: the number is 0, Result is n-1 and the pass
                 * criterion n-2, so the description is 1..n-3. */
                for (unsigned c = 1U; (c + 2U) < n; c++) {
                    char part[PROTO_TEXT_MAX];
                    copy_trimmed(part, sizeof part, cells[c], lens[c]);
                    const size_t plen = strlen(part);
                    if (plen == 0U) {
                        continue;
                    }
                    if ((used != 0U) && ((used + 3U) < sizeof st->text)) {
                        memcpy(&st->text[used], " - ", 3);
                        used += 3U;
                    }
                    size_t room = sizeof st->text - used - 1U;
                    if (plen < room) {
                        room = plen;
                    }
                    memcpy(&st->text[used], part, room);
                    used += room;
                    st->text[used] = '\0';
                }
                s_proto.count++;
            }
        }

        pos = eol + 1U;
    }

    s_proto.cursor   = 0;
    s_proto.loaded   = (s_proto.count > 0U);
    s_proto.finished = !s_proto.loaded;
    return s_proto.loaded;
}

bool protocol_load_embedded(void)
{
    return protocol_load((const char *)protocol_doc, (size_t)protocol_doc_len, protocol_doc_name);
}

const protocol_t *protocol_get(void)
{
    return &s_proto;
}

const proto_step_t *protocol_current(void)
{
    if (!s_proto.loaded || s_proto.finished || (s_proto.cursor >= s_proto.count)) {
        return NULL;
    }
    return &s_proto.steps[s_proto.cursor];
}

bool protocol_record(uint16_t index, bool passed, const char **why)
{
    static const char *const no_doc   = "no protocol loaded";
    static const char *const over     = "the run is over; restart it to run again";
    static const char *const range    = "no such step in this protocol";
    static const char *const already  = "that step already has a result; a recorded outcome "
                                        "is not revised here";
    static const char *const not_due  = "out of order: the previous step has not passed";

    if (why != NULL) {
        *why = NULL;
    }
    if (!s_proto.loaded) {
        if (why != NULL) {
            *why = no_doc;
        }
        return false;
    }
    if (s_proto.finished) {
        if (why != NULL) {
            *why = over;
        }
        return false;
    }
    if (index >= s_proto.count) {
        if (why != NULL) {
            *why = range;
        }
        return false;
    }
    if (s_proto.steps[index].state != PROTO_PENDING) {
        if (why != NULL) {
            *why = already;
        }
        return false;
    }
    /* The whole of S10 and S11 is this comparison: only the step whose
     * precondition has passed may run, so a skip and a reorder are the same
     * refusal, and there is no path that records a result for anything else. */
    if (index != s_proto.cursor) {
        if (why != NULL) {
            *why = not_due;
        }
        return false;
    }

    s_proto.steps[index].state = passed ? PROTO_PASSED : PROTO_FAILED;
    if (!passed) {
        /* A failed step is where the run stops: the next step's precondition is
         * this one's pass, so continuing would produce a record of a procedure
         * that was not the procedure. */
        s_proto.finished = true;
    } else {
        s_proto.cursor++;
        if (s_proto.cursor >= s_proto.count) {
            s_proto.finished = true;
        }
    }
    return true;
}

void protocol_restart(void)
{
    (void)protocol_load_embedded();
}

void protocol_set_instance(const char *s)
{
    copy_trimmed(s_proto.instance, sizeof s_proto.instance, s, (s != NULL) ? strlen(s) : 0U);
}

const char *protocol_instance(void)
{
    return s_proto.instance;
}

/* --- emission (SERVICE-TOOL F4) -------------------------------------------
 *
 * The record is the carried document's step tables with this run's results
 * (S22), naming the procedure verbatim (S23) and the instance the run was given
 * (S24). It carries no run date: the tool has no clock and no time reference,
 * and the filing call's doc_date is where the date belongs -- inventing one, or
 * emitting a placeholder for a human to fill, would both stop the record filing
 * as emitted.
 *
 * Streamed a line at a time. Nothing is buffered whole and nothing survives the
 * call (S27).
 */
bool protocol_emit(void (*out)(const char *line), const char **why)
{
    static const char *const no_doc   = "no protocol loaded";
    static const char *const unfinished = "the run is not finished";
    static const char *const no_inst  = "no instance fixed for this run; the ERP issues the "
                                        "serial and this tool does not";

    if (why != NULL) {
        *why = NULL;
    }
    if (out == NULL) {
        return false;
    }
    if (!s_proto.loaded) {
        if (why != NULL) {
            *why = no_doc;
        }
        return false;
    }
    if (!s_proto.finished) {
        if (why != NULL) {
            *why = unfinished;
        }
        return false;
    }
    if (s_proto.instance[0] == '\0') {
        if (why != NULL) {
            *why = no_inst;
        }
        return false;
    }

    char line[PROTO_CRIT_MAX + PROTO_TEXT_MAX + 64];

    (void)snprintf(line, sizeof line, "# %s - Quality Protocol", s_proto.instance);
    out(line);
    out("");
    out("Result columns are one run of the type-level protocol named below, performed step");
    out("by step. The run date is the `doc_date` of the filing call.");
    out("");
    out("| Field | Value |");
    out("|---|---|");
    (void)snprintf(line, sizeof line, "| Instance | `%s` |", s_proto.instance);
    out(line);
    (void)snprintf(line, sizeof line, "| Procedure | `%s` |", s_proto.identity);
    out(line);
    (void)snprintf(line, sizeof line, "| Steps | %u of %u passed, %u failed |",
                   (unsigned)protocol_passed_count(), (unsigned)s_proto.count,
                   (unsigned)protocol_failed_count());
    out(line);
    (void)snprintf(line, sizeof line, "| Outcome | %s |",
                   (protocol_failed_count() > 0U) ? "STOPPED at a failed step" : "complete");
    out(line);
    char serial[28];
    tool_serial(serial, sizeof serial);
    (void)snprintf(line, sizeof line, "| Performed with | service tool, board serial `%s` |",
                   serial);
    out(line);

    const char *section = NULL;
    for (uint16_t i = 0; i < s_proto.count; i++) {
        const proto_step_t *st = &s_proto.steps[i];

        if ((section == NULL) || (strcmp(section, st->section) != 0)) {
            section = st->section;
            out("");
            (void)snprintf(line, sizeof line, "## %s", st->section);
            out(line);
            out("");
            out("| # | Step | Observed | Result |");
            out("|---|---|---|---|");
        }

        const char *result = "not reached";
        if (st->state == PROTO_PASSED) {
            result = "Pass";
        } else if (st->state == PROTO_FAILED) {
            result = "Fail";
        }

        /* S25: the tool measures nothing per step yet, so every outcome here is
         * an operator judgement and says so. A measured value replaces this
         * text with the value, which is what makes the two distinguishable. */
        const char *observed =
            (st->state == PROTO_PENDING) ? "-" : "judged by operator against the criterion";

        (void)snprintf(line, sizeof line, "| %u | %s | %s | %s |", (unsigned)st->number, st->text,
                       observed, result);
        out(line);
    }

    out("");
    return true;
}

uint16_t protocol_passed_count(void)
{
    uint16_t n = 0;
    for (uint16_t i = 0; i < s_proto.count; i++) {
        if (s_proto.steps[i].state == PROTO_PASSED) {
            n++;
        }
    }
    return n;
}

uint16_t protocol_failed_count(void)
{
    uint16_t n = 0;
    for (uint16_t i = 0; i < s_proto.count; i++) {
        if (s_proto.steps[i].state == PROTO_FAILED) {
            n++;
        }
    }
    return n;
}
