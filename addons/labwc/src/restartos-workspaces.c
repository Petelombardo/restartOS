/*
 * restartos-workspaces: labwc desktop buttons for waybar.
 *
 * Debian's waybar has no module for the ext-workspace-v1 protocol labwc speaks,
 * so each desktop button is a waybar custom module running this:
 *
 *   restartos-workspaces watch N     print a waybar JSON line for desktop N
 *                                    ({"text","class":"active|inactive|urgent"})
 *                                    whenever its state changes; runs until labwc exits
 *   restartos-workspaces activate N  switch to desktop N
 *   restartos-workspaces list        print the desktops, '*' marks the current one
 *
 * Desktops are numbered from 1 in the order labwc announces them (its rc.xml order).
 * Built by the labwc addon's post-build.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include "ext-workspace-v1-client-protocol.h"

struct desktop {
	struct ext_workspace_handle_v1 *handle;
	char *name;
	uint32_t state;
	int removed;
	struct desktop *next;
};

static struct ext_workspace_manager_v1 *manager;
static struct desktop *desktops;
static int got_done;

static void ws_id(void *data, struct ext_workspace_handle_v1 *h, const char *id) {}
static void ws_coordinates(void *data, struct ext_workspace_handle_v1 *h, struct wl_array *c) {}
static void ws_capabilities(void *data, struct ext_workspace_handle_v1 *h, uint32_t caps) {}

static void ws_name(void *data, struct ext_workspace_handle_v1 *h, const char *name)
{
	struct desktop *d = data;
	free(d->name);
	d->name = strdup(name);
}

static void ws_state(void *data, struct ext_workspace_handle_v1 *h, uint32_t state)
{
	((struct desktop *)data)->state = state;
}

static void ws_removed(void *data, struct ext_workspace_handle_v1 *h)
{
	((struct desktop *)data)->removed = 1;
}

static const struct ext_workspace_handle_v1_listener ws_listener = {
	.id = ws_id,
	.name = ws_name,
	.coordinates = ws_coordinates,
	.state = ws_state,
	.capabilities = ws_capabilities,
	.removed = ws_removed,
};

static void group_capabilities(void *data, struct ext_workspace_group_handle_v1 *g, uint32_t caps) {}
static void group_output(void *data, struct ext_workspace_group_handle_v1 *g, struct wl_output *o) {}
static void group_workspace(void *data, struct ext_workspace_group_handle_v1 *g,
		struct ext_workspace_handle_v1 *w) {}
static void group_removed(void *data, struct ext_workspace_group_handle_v1 *g) {}

static const struct ext_workspace_group_handle_v1_listener group_listener = {
	.capabilities = group_capabilities,
	.output_enter = group_output,
	.output_leave = group_output,
	.workspace_enter = group_workspace,
	.workspace_leave = group_workspace,
	.removed = group_removed,
};

static void mgr_group(void *data, struct ext_workspace_manager_v1 *m,
		struct ext_workspace_group_handle_v1 *g)
{
	ext_workspace_group_handle_v1_add_listener(g, &group_listener, NULL);
}

static void mgr_workspace(void *data, struct ext_workspace_manager_v1 *m,
		struct ext_workspace_handle_v1 *h)
{
	struct desktop *d = calloc(1, sizeof(*d)), **tail = &desktops;
	d->handle = h;
	while (*tail)
		tail = &(*tail)->next;
	*tail = d;
	ext_workspace_handle_v1_add_listener(h, &ws_listener, d);
}

static void mgr_done(void *data, struct ext_workspace_manager_v1 *m)
{
	got_done = 1;
}

static void mgr_finished(void *data, struct ext_workspace_manager_v1 *m)
{
	exit(0);
}

static const struct ext_workspace_manager_v1_listener mgr_listener = {
	.workspace_group = mgr_group,
	.workspace = mgr_workspace,
	.done = mgr_done,
	.finished = mgr_finished,
};

static void global(void *data, struct wl_registry *r, uint32_t name,
		const char *iface, uint32_t version)
{
	if (strcmp(iface, ext_workspace_manager_v1_interface.name) == 0) {
		manager = wl_registry_bind(r, name, &ext_workspace_manager_v1_interface, 1);
		ext_workspace_manager_v1_add_listener(manager, &mgr_listener, NULL);
	}
}

static void global_remove(void *data, struct wl_registry *r, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
	.global = global,
	.global_remove = global_remove,
};

/* Desktop n (from 1), skipping removed ones */
static struct desktop *nth(int n)
{
	for (struct desktop *d = desktops; d; d = d->next)
		if (!d->removed && --n == 0)
			return d;
	return NULL;
}

static void print_button(int n)
{
	static char last[512];
	char line[512], name[128] = "", text[12] = "";
	struct desktop *d = nth(n);
	const char *class = "inactive";

	/* no such desktop: empty text, and waybar hides the module */
	if (d) {
		snprintf(text, sizeof(text), "%d", n);
		/* tooltip is the desktop's name; keep it JSON-safe */
		size_t j = 0;
		for (const char *s = d->name ? d->name : ""; *s && j < sizeof(name) - 1; s++)
			if (*s != '"' && *s != '\\' && (unsigned char)*s >= ' ')
				name[j++] = *s;
		name[j] = 0;
		if (d->state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE)
			class = "active";
		else if (d->state & EXT_WORKSPACE_HANDLE_V1_STATE_URGENT)
			class = "urgent";
	}
	snprintf(line, sizeof(line), "{\"text\":\"%s\",\"class\":\"%s\",\"tooltip\":\"%s\"}",
		text, class, name);
	if (strcmp(line, last) != 0) {
		strcpy(last, line);
		puts(line);
		fflush(stdout);
	}
}

static void usage(void)
{
	fprintf(stderr, "usage: restartos-workspaces watch N | activate N | list\n");
	exit(2);
}

int main(int argc, char **argv)
{
	if (argc < 2)
		usage();
	const char *cmd = argv[1];
	int n = argc > 2 ? atoi(argv[2]) : 0;
	if (strcmp(cmd, "list") != 0 && n < 1)
		usage();

	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "restartos-workspaces: no Wayland display\n");
		return 1;
	}
	wl_registry_add_listener(wl_display_get_registry(display), &registry_listener, NULL);
	wl_display_roundtrip(display);
	if (!manager) {
		fprintf(stderr, "restartos-workspaces: compositor has no ext-workspace-v1\n");
		return 1;
	}
	while (!got_done)
		if (wl_display_dispatch(display) < 0)
			return 1;

	if (strcmp(cmd, "list") == 0) {
		int i = 0;
		for (struct desktop *d = desktops; d; d = d->next)
			if (!d->removed)
				printf("%c %d %s\n",
					d->state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE ? '*' : ' ',
					++i, d->name ? d->name : "");
		return 0;
	}
	if (strcmp(cmd, "activate") == 0) {
		struct desktop *d = nth(n);
		if (!d) {
			fprintf(stderr, "restartos-workspaces: no desktop %d\n", n);
			return 1;
		}
		ext_workspace_handle_v1_activate(d->handle);
		ext_workspace_manager_v1_commit(manager);
		wl_display_roundtrip(display);
		return 0;
	}
	if (strcmp(cmd, "watch") != 0)
		usage();
	for (;;) {
		got_done = 0;
		print_button(n);
		while (!got_done)
			if (wl_display_dispatch(display) < 0)
				return 1;
	}
}
