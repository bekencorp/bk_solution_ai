/**
 * @file ai_debug_cli.h
 * @brief Debug CLI for the AI dialogue / vision pages.
 *
 * Registers an `aiui` command group with subcommands:
 *   - aiui chat                    enter page_6 (AI chat)
 *   - aiui vision                  enter page_7 (vision recognition)
 *   - aiui back                    return to page_3
 *   - aiui state                   dump mic/spk active + level + last AI evt
 *   - aiui evt {listen|think|speak|idle}
 *                                  inject an APP_EVT_AI_* event (UI test)
 *
 * Intended for development; safe to leave enabled in release builds (the
 * commands are gated by CONFIG_CLI at compile time).
 */
#ifndef _AI_DEBUG_CLI_H_
#define _AI_DEBUG_CLI_H_

#ifdef __cplusplus
extern "C" {
#endif

void ai_debug_cli_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _AI_DEBUG_CLI_H_ */
