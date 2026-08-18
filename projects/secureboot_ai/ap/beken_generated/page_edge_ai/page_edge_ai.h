/**
 * @file page_edge_ai.h
 * @brief Edge AI submenu and solution-example page.
 */
#ifndef __PAGE_EDGE_AI_H__
#define __PAGE_EDGE_AI_H__

#ifdef __cplusplus
extern "C" {
#endif

int page_edge_ai_enter(void);
int page_edge_ai_solution_enter(void);
int page_edge_ai_archive_enter(void);
void page_edge_ai_solution_set_status(const char *text);
void page_edge_ai_solution_destroy(void);

#ifdef __cplusplus
}
#endif

#endif /* __PAGE_EDGE_AI_H__ */
