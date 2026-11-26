// SPDX-License-Identifier: MIT
// "What is happening?": the CYD Survival Wiki's triage questions, from the pack's triage table.
// Each answer leads to another question (a node id) or opens a guide ("id:N").
#include "wiki.h"
#include <stdio.h>
#include <string.h>

static struct tri_node nodes[32];
static int nnodes, tpack = -1;

int triage_load(void)
{
	nnodes = 0;
	tpack = -1;
	for (int p = 0; p < npacks && tpack < 0; p++) {
		sqlite3_stmt *st;
		if (sqlite3_prepare_v2(packs[p].db, "SELECT id, question, answers FROM triage ORDER BY ord", -1, &st, NULL) != SQLITE_OK) continue;
		while (sqlite3_step(st) == SQLITE_ROW && nnodes < 32) {
			struct tri_node *t = &nodes[nnodes++];
			memset(t, 0, sizeof(*t));
			scopy(t->id, sizeof(t->id), col(st, 0));
			scopy(t->q, sizeof(t->q), col(st, 1));
			char a[1024];
			scopy(a, sizeof(a), col(st, 2));
			for (char *line = strtok(a, "\n"); line && t->n < 5; line = strtok(NULL, "\n")) {
				char *tab = strchr(line, '\t');
				if (!tab) continue;
				*tab = 0;
				snprintf(t->label[t->n], sizeof(t->label[0]), "%s", line);
				snprintf(t->to[t->n], sizeof(t->to[0]), "%s", tab + 1);
				t->n++;
			}
		}
		sqlite3_finalize(st);
		if (nnodes) tpack = p;
	}
	return nnodes;
}

const struct tri_node *triage_node(const char *id)
{
	for (int i = 0; i < nnodes; i++) if (!strcmp(nodes[i].id, id)) return &nodes[i];
	return NULL;
}

int triage_pack(void) { return tpack; }
