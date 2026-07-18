#include "xdg_session.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/util/log.h>

#if defined(__has_include)
#if __has_include("xdg-session-management-v1-protocol.h")
#include "xdg-session-management-v1-protocol.h"
#define MANGO_HAS_XDG_SESSION_MANAGEMENT_PROTOCOL 1
#endif
#endif

#ifndef MANGO_HAS_XDG_SESSION_MANAGEMENT_PROTOCOL
#define MANGO_HAS_XDG_SESSION_MANAGEMENT_PROTOCOL 0
#endif

enum mango_xdg_session_manager_error {
	MANGO_XDG_SESSION_MANAGER_ERROR_IN_USE = 1,
	MANGO_XDG_SESSION_MANAGER_ERROR_INVALID_SESSION_ID = 2,
	MANGO_XDG_SESSION_MANAGER_ERROR_INVALID_REASON = 3,
};

enum mango_xdg_session_error {
	MANGO_XDG_SESSION_ERROR_NAME_IN_USE = 1,
	MANGO_XDG_SESSION_ERROR_ALREADY_MAPPED = 2,
	MANGO_XDG_SESSION_ERROR_INVALID_NAME = 3,
	MANGO_XDG_SESSION_ERROR_ALREADY_ADDED = 4,
};

enum mango_xdg_session_reason {
	MANGO_XDG_SESSION_REASON_LAUNCH = 1,
	MANGO_XDG_SESSION_REASON_RECOVER = 2,
	MANGO_XDG_SESSION_REASON_SESSION_RESTORE = 3,
};

struct mango_xdg_toplevel_session;
struct mango_xdg_session_resource;

struct mango_xdg_session_store {
	struct wl_list link;
	struct wl_list toplevels;
	char *id;
	struct mango_xdg_session_resource *active;
};

struct mango_xdg_toplevel_state {
	struct wl_list link;
	struct wl_list handles;
	struct mango_xdg_session_store *store;
	char *name;
	struct wl_resource *bound_toplevel;
	struct wl_listener bound_toplevel_destroy;
};

struct mango_xdg_session_resource {
	struct wl_resource *resource;
	struct wl_client *client;
	struct mango_xdg_session_store *store;
	bool inert;
	struct wl_list handles;
};

struct mango_xdg_toplevel_session {
	struct wl_resource *resource;
	struct mango_xdg_session_resource *session;
	struct mango_xdg_toplevel_state *state;
	bool inert;
	bool restore_requested;
	struct wl_list session_link;
	struct wl_list state_link;
};

struct mango_xdg_session_manager {
	struct wl_display *display;
	struct wl_global *global;
	struct wl_list sessions;
	uint64_t next_session_serial;
	bool initialized;
};

static struct mango_xdg_session_manager xdg_session_manager;

static bool mango_xdg_session_reason_is_valid(uint32_t reason);
static bool mango_xdg_session_utf8_is_valid(const char *value);
static void mango_xdg_session_store_destroy(
	struct mango_xdg_session_store *store);
static void mango_xdg_session_resource_make_inert(
	struct mango_xdg_session_resource *session);
static void mango_xdg_toplevel_session_make_inert(
	struct mango_xdg_toplevel_session *toplevel_session);
static void mango_xdg_toplevel_state_destroy(
	struct mango_xdg_toplevel_state *state);

#if MANGO_HAS_XDG_SESSION_MANAGEMENT_PROTOCOL
static void mango_xdg_session_resource_destroy(struct wl_resource *resource);
static void mango_xdg_toplevel_session_resource_destroy(
	struct wl_resource *resource);
static void mango_xdg_session_manager_handle_destroy(
	struct wl_client *client, struct wl_resource *resource);
static void mango_xdg_session_manager_handle_get_session(
	struct wl_client *client, struct wl_resource *resource, uint32_t id,
	uint32_t reason, const char *session_id);
static void mango_xdg_session_handle_destroy(
	struct wl_client *client, struct wl_resource *resource);
static void mango_xdg_session_handle_remove(
	struct wl_client *client, struct wl_resource *resource);
static void mango_xdg_session_handle_add_toplevel(
	struct wl_client *client, struct wl_resource *resource, uint32_t id,
	struct wl_resource *toplevel, const char *name);
static void mango_xdg_session_handle_restore_toplevel(
	struct wl_client *client, struct wl_resource *resource, uint32_t id,
	struct wl_resource *toplevel, const char *name);
static void mango_xdg_session_handle_remove_toplevel(
	struct wl_client *client, struct wl_resource *resource, const char *name);
static void mango_xdg_toplevel_session_handle_destroy(
	struct wl_client *client, struct wl_resource *resource);
static void mango_xdg_toplevel_session_handle_rename(
	struct wl_client *client, struct wl_resource *resource, const char *name);

static const struct xdg_session_manager_v1_interface
	mango_xdg_session_manager_impl = {
		.destroy = mango_xdg_session_manager_handle_destroy,
		.get_session = mango_xdg_session_manager_handle_get_session,
};

static const struct xdg_session_v1_interface mango_xdg_session_impl = {
		.destroy = mango_xdg_session_handle_destroy,
		.remove = mango_xdg_session_handle_remove,
		.add_toplevel = mango_xdg_session_handle_add_toplevel,
		.restore_toplevel = mango_xdg_session_handle_restore_toplevel,
		.remove_toplevel = mango_xdg_session_handle_remove_toplevel,
};

static const struct xdg_toplevel_session_v1_interface
	mango_xdg_toplevel_session_impl = {
		.destroy = mango_xdg_toplevel_session_handle_destroy,
		.rename = mango_xdg_toplevel_session_handle_rename,
};
#endif

static bool mango_xdg_session_reason_is_valid(uint32_t reason) {
	switch (reason) {
	case MANGO_XDG_SESSION_REASON_LAUNCH:
	case MANGO_XDG_SESSION_REASON_RECOVER:
	case MANGO_XDG_SESSION_REASON_SESSION_RESTORE:
		return true;
	default:
		return false;
	}
}

static bool mango_xdg_session_utf8_is_valid(const char *value) {
	const unsigned char *bytes = (const unsigned char *)value;

	if (!bytes) {
		return false;
	}

	while (*bytes) {
		unsigned char lead = bytes[0];
		size_t needed = 0;

		if (lead <= 0x7f) {
			bytes++;
			continue;
		}

		if ((lead & 0xe0) == 0xc0) {
			if (lead < 0xc2) {
				return false;
			}
			needed = 1;
		} else if ((lead & 0xf0) == 0xe0) {
			needed = 2;
		} else if ((lead & 0xf8) == 0xf0) {
			if (lead > 0xf4) {
				return false;
			}
			needed = 3;
		} else {
			return false;
		}

		for (size_t i = 1; i <= needed; i++) {
			if (bytes[i] == '\0' || (bytes[i] & 0xc0) != 0x80) {
				return false;
			}
		}

		if ((lead == 0xe0 && bytes[1] < 0xa0) ||
			(lead == 0xed && bytes[1] > 0x9f) ||
			(lead == 0xf0 && bytes[1] < 0x90) ||
			(lead == 0xf4 && bytes[1] > 0x8f)) {
			return false;
		}

		bytes += needed + 1;
	}

	return true;
}

static struct mango_xdg_session_store *mango_xdg_session_store_find(
	const char *session_id) {
	struct mango_xdg_session_store *store;

	wl_list_for_each(store, &xdg_session_manager.sessions, link) {
		if (strcmp(store->id, session_id) == 0) {
			return store;
		}
	}

	return NULL;
}

static struct mango_xdg_toplevel_state *mango_xdg_toplevel_state_find_by_name(
	struct mango_xdg_session_store *store, const char *name) {
	struct mango_xdg_toplevel_state *state;

	wl_list_for_each(state, &store->toplevels, link) {
		if (strcmp(state->name, name) == 0) {
			return state;
		}
	}

	return NULL;
}

static struct mango_xdg_toplevel_state *
mango_xdg_toplevel_state_find_by_resource(struct wl_resource *toplevel) {
	struct mango_xdg_session_store *store;

	wl_list_for_each(store, &xdg_session_manager.sessions, link) {
		struct mango_xdg_toplevel_state *state;

		wl_list_for_each(state, &store->toplevels, link) {
			if (state->bound_toplevel == toplevel) {
				return state;
			}
		}
	}

	return NULL;
}

static void mango_xdg_toplevel_state_clear_bound_resource(
	struct mango_xdg_toplevel_state *state) {
	if (!state->bound_toplevel) {
		return;
	}

	wl_list_remove(&state->bound_toplevel_destroy.link);
	wl_list_init(&state->bound_toplevel_destroy.link);
	state->bound_toplevel = NULL;
}

static void mango_xdg_toplevel_state_bound_resource_destroy(
	struct wl_listener *listener, void *data) {
	struct mango_xdg_toplevel_state *state =
		wl_container_of(listener, state, bound_toplevel_destroy);

	(void)data;
	wl_list_remove(&state->bound_toplevel_destroy.link);
	state->bound_toplevel = NULL;
	wl_list_init(&state->bound_toplevel_destroy.link);
}

static bool mango_xdg_toplevel_state_bind_resource(
	struct mango_xdg_toplevel_state *state, struct wl_resource *toplevel) {
	if (state->bound_toplevel == toplevel) {
		return true;
	}

	if (state->bound_toplevel) {
		return false;
	}

	state->bound_toplevel = toplevel;
	state->bound_toplevel_destroy.notify =
		mango_xdg_toplevel_state_bound_resource_destroy;
	wl_list_init(&state->bound_toplevel_destroy.link);
	wl_resource_add_destroy_listener(toplevel, &state->bound_toplevel_destroy);
	return true;
}

static char *mango_xdg_session_generate_id(void) {
	char buffer[128];
	struct timespec ts = {0};
	int length;

	clock_gettime(CLOCK_REALTIME, &ts);
	length = snprintf(buffer, sizeof(buffer), "mango-%ld-%ld-%d-%" PRIu64,
					  (long)ts.tv_sec, (long)ts.tv_nsec, (int)getpid(),
					  xdg_session_manager.next_session_serial++);
	if (length < 0 || (size_t)length >= sizeof(buffer)) {
		return NULL;
	}

	return strdup(buffer);
}

static struct mango_xdg_session_store *mango_xdg_session_store_create(void) {
	struct mango_xdg_session_store *store = calloc(1, sizeof(*store));

	if (!store) {
		return NULL;
	}

	store->id = mango_xdg_session_generate_id();
	if (!store->id) {
		free(store);
		return NULL;
	}

	wl_list_init(&store->toplevels);
	wl_list_insert(&xdg_session_manager.sessions, &store->link);
	return store;
}

#if MANGO_HAS_XDG_SESSION_MANAGEMENT_PROTOCOL
static struct mango_xdg_session_resource *mango_xdg_session_resource_create(
	struct wl_client *client, struct wl_resource *manager_resource, uint32_t id,
	struct mango_xdg_session_store *store) {
	struct mango_xdg_session_resource *session;
	struct wl_resource *resource = wl_resource_create(
		client, &xdg_session_v1_interface, wl_resource_get_version(manager_resource),
		id);

	if (!resource) {
		wl_resource_post_no_memory(manager_resource);
		return NULL;
	}

	session = calloc(1, sizeof(*session));
	if (!session) {
		wl_resource_destroy(resource);
		wl_resource_post_no_memory(manager_resource);
		return NULL;
	}

	session->resource = resource;
	session->client = client;
	session->store = store;
	wl_list_init(&session->handles);
	wl_resource_set_implementation(resource, &mango_xdg_session_impl, session,
								   mango_xdg_session_resource_destroy);
	return session;
}
#endif

static struct mango_xdg_toplevel_state *mango_xdg_toplevel_state_create(
	struct mango_xdg_session_store *store, const char *name) {
	struct mango_xdg_toplevel_state *state = calloc(1, sizeof(*state));

	if (!state) {
		return NULL;
	}

	state->name = strdup(name);
	if (!state->name) {
		free(state);
		return NULL;
	}

	state->store = store;
	wl_list_init(&state->handles);
	wl_list_init(&state->bound_toplevel_destroy.link);
	wl_list_insert(&store->toplevels, &state->link);
	return state;
}

#if MANGO_HAS_XDG_SESSION_MANAGEMENT_PROTOCOL
static struct mango_xdg_toplevel_session *
mango_xdg_toplevel_session_create(struct wl_client *client,
								  struct wl_resource *session_resource,
								  uint32_t id,
								  struct mango_xdg_session_resource *session,
								  struct mango_xdg_toplevel_state *state,
								  bool restore_requested) {
	struct mango_xdg_toplevel_session *toplevel_session;
	struct wl_resource *resource = wl_resource_create(
		client, &xdg_toplevel_session_v1_interface,
		wl_resource_get_version(session_resource), id);

	if (!resource) {
		wl_resource_post_no_memory(session_resource);
		return NULL;
	}

	toplevel_session = calloc(1, sizeof(*toplevel_session));
	if (!toplevel_session) {
		wl_resource_destroy(resource);
		wl_resource_post_no_memory(session_resource);
		return NULL;
	}

	toplevel_session->resource = resource;
	toplevel_session->session = session;
	toplevel_session->state = state;
	toplevel_session->restore_requested = restore_requested;
	wl_list_init(&toplevel_session->session_link);
	wl_list_init(&toplevel_session->state_link);
	if (session) {
		wl_list_insert(&session->handles, &toplevel_session->session_link);
	}
	if (state) {
		wl_list_insert(&state->handles, &toplevel_session->state_link);
	}
	wl_resource_set_implementation(resource, &mango_xdg_toplevel_session_impl,
								   toplevel_session,
								   mango_xdg_toplevel_session_resource_destroy);
	return toplevel_session;
}
#endif

static void mango_xdg_toplevel_session_make_inert(
	struct mango_xdg_toplevel_session *toplevel_session) {
	if (toplevel_session->inert) {
		return;
	}

	toplevel_session->inert = true;
	toplevel_session->session = NULL;
	if (!wl_list_empty(&toplevel_session->session_link)) {
		wl_list_remove(&toplevel_session->session_link);
		wl_list_init(&toplevel_session->session_link);
	}
}

static void mango_xdg_session_resource_make_inert(
	struct mango_xdg_session_resource *session) {
	struct mango_xdg_toplevel_session *toplevel_session, *tmp;

	if (session->inert) {
		return;
	}

	session->inert = true;
	if (session->store && session->store->active == session) {
		session->store->active = NULL;
	}
	session->store = NULL;

	wl_list_for_each_safe(toplevel_session, tmp, &session->handles, session_link) {
		mango_xdg_toplevel_session_make_inert(toplevel_session);
	}
}

static void mango_xdg_toplevel_state_destroy(
	struct mango_xdg_toplevel_state *state) {
	struct mango_xdg_toplevel_session *toplevel_session, *tmp;

	mango_xdg_toplevel_state_clear_bound_resource(state);
	wl_list_for_each_safe(toplevel_session, tmp, &state->handles, state_link) {
		mango_xdg_toplevel_session_make_inert(toplevel_session);
		wl_list_remove(&toplevel_session->state_link);
		wl_list_init(&toplevel_session->state_link);
		toplevel_session->state = NULL;
	}

	wl_list_remove(&state->link);
	free(state->name);
	free(state);
}

static void mango_xdg_session_store_destroy(
	struct mango_xdg_session_store *store) {
	struct mango_xdg_toplevel_state *state, *tmp;

	if (store->active) {
		mango_xdg_session_resource_make_inert(store->active);
	}

	wl_list_for_each_safe(state, tmp, &store->toplevels, link) {
		mango_xdg_toplevel_state_destroy(state);
	}

	wl_list_remove(&store->link);
	free(store->id);
	free(store);
}

#if MANGO_HAS_XDG_SESSION_MANAGEMENT_PROTOCOL
static void mango_xdg_session_manager_resource_destroy(
	struct wl_resource *resource) {
	(void)resource;
}

static void mango_xdg_session_resource_destroy(struct wl_resource *resource) {
	struct mango_xdg_session_resource *session =
		wl_resource_get_user_data(resource);

	if (!session) {
		return;
	}

	mango_xdg_session_resource_make_inert(session);
	free(session);
}

static void mango_xdg_toplevel_session_resource_destroy(
	struct wl_resource *resource) {
	struct mango_xdg_toplevel_session *toplevel_session =
		wl_resource_get_user_data(resource);

	if (!toplevel_session) {
		return;
	}

	if (!wl_list_empty(&toplevel_session->session_link)) {
		wl_list_remove(&toplevel_session->session_link);
		wl_list_init(&toplevel_session->session_link);
	}
	if (!wl_list_empty(&toplevel_session->state_link)) {
		wl_list_remove(&toplevel_session->state_link);
		wl_list_init(&toplevel_session->state_link);
	}
	free(toplevel_session);
}

static void mango_xdg_session_manager_bind(struct wl_client *client, void *data,
										   uint32_t version, uint32_t id) {
	struct wl_resource *resource;

	(void)data;
	resource =
		wl_resource_create(client, &xdg_session_manager_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &mango_xdg_session_manager_impl,
								   NULL,
								   mango_xdg_session_manager_resource_destroy);
}

static void mango_xdg_session_manager_handle_destroy(
	struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	wl_resource_destroy(resource);
}

static void mango_xdg_session_manager_handle_get_session(
	struct wl_client *client, struct wl_resource *resource, uint32_t id,
	uint32_t reason, const char *session_id) {
	struct mango_xdg_session_store *store = NULL;
	struct mango_xdg_session_resource *session;
	bool restored = false;

	if (!mango_xdg_session_reason_is_valid(reason)) {
		wl_resource_post_error(
			resource, MANGO_XDG_SESSION_MANAGER_ERROR_INVALID_REASON,
			"xdg_session_manager_v1.get_session received invalid reason %u",
			reason);
		return;
	}

	if (session_id && !mango_xdg_session_utf8_is_valid(session_id)) {
		wl_resource_post_error(
			resource, MANGO_XDG_SESSION_MANAGER_ERROR_INVALID_SESSION_ID,
			"xdg_session_manager_v1.get_session received an invalid session id");
		return;
	}

	if (session_id) {
		store = mango_xdg_session_store_find(session_id);
		if (store) {
			restored = true;
			if (store->active) {
				if (store->active->client == client) {
					wl_resource_post_error(
						resource, MANGO_XDG_SESSION_MANAGER_ERROR_IN_USE,
						"session '%s' is already in use by this client", session_id);
					return;
				}

				xdg_session_v1_send_replaced(store->active->resource);
				mango_xdg_session_resource_make_inert(store->active);
			}
		}
	}

	if (!store) {
		store = mango_xdg_session_store_create();
		if (!store) {
			wl_resource_post_no_memory(resource);
			return;
		}
	}

	session = mango_xdg_session_resource_create(client, resource, id, store);
	if (!session) {
		if (!restored && !store->active &&
			wl_list_empty(&store->toplevels)) {
			mango_xdg_session_store_destroy(store);
		}
		return;
	}

	store->active = session;

	if (restored) {
		xdg_session_v1_send_restored(session->resource);
	} else {
		xdg_session_v1_send_created(session->resource, store->id);
	}
}

static void mango_xdg_session_handle_destroy(
	struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	wl_resource_destroy(resource);
}

static void mango_xdg_session_handle_remove(
	struct wl_client *client, struct wl_resource *resource) {
	struct mango_xdg_session_resource *session =
		wl_resource_get_user_data(resource);
	struct mango_xdg_session_store *store;

	(void)client;
	if (!session) {
		wl_resource_destroy(resource);
		return;
	}

	store = session->store;
	if (store) {
		mango_xdg_session_store_destroy(store);
	}

	wl_resource_destroy(resource);
}

static bool mango_xdg_session_validate_name(struct wl_resource *resource,
											const char *name) {
	if (!mango_xdg_session_utf8_is_valid(name)) {
		wl_resource_post_error(
			resource, MANGO_XDG_SESSION_ERROR_INVALID_NAME,
			"xdg-session-management received an invalid toplevel name");
		return false;
	}

	return true;
}

static void mango_xdg_session_handle_add_toplevel(
	struct wl_client *client, struct wl_resource *resource, uint32_t id,
	struct wl_resource *toplevel, const char *name) {
	struct mango_xdg_session_resource *session =
		wl_resource_get_user_data(resource);
	struct mango_xdg_toplevel_state *existing;
	struct mango_xdg_toplevel_state *state;

	if (!mango_xdg_session_validate_name(resource, name)) {
		return;
	}

	if (!session || session->inert || !session->store) {
		(void)mango_xdg_toplevel_session_create(client, resource, id, NULL, NULL,
												 false);
		return;
	}

	if (mango_xdg_toplevel_state_find_by_resource(toplevel)) {
		wl_resource_post_error(
			resource, MANGO_XDG_SESSION_ERROR_ALREADY_ADDED,
			"xdg_toplevel has already been added to a session");
		return;
	}

	existing = mango_xdg_toplevel_state_find_by_name(session->store, name);
	if (existing) {
		wl_resource_post_error(
			resource, MANGO_XDG_SESSION_ERROR_NAME_IN_USE,
			"toplevel name '%s' is already in use in this session", name);
		return;
	}

	state = mango_xdg_toplevel_state_create(session->store, name);
	if (!state) {
		wl_resource_post_no_memory(resource);
		return;
	}

	if (!mango_xdg_toplevel_state_bind_resource(state, toplevel)) {
		mango_xdg_toplevel_state_destroy(state);
		wl_resource_post_error(
			resource, MANGO_XDG_SESSION_ERROR_ALREADY_ADDED,
			"xdg_toplevel is already bound to another session entry");
		return;
	}

	if (!mango_xdg_toplevel_session_create(client, resource, id, session, state,
										   false)) {
		mango_xdg_toplevel_state_destroy(state);
	}
}

static void mango_xdg_session_handle_restore_toplevel(
	struct wl_client *client, struct wl_resource *resource, uint32_t id,
	struct wl_resource *toplevel, const char *name) {
	struct mango_xdg_session_resource *session =
		wl_resource_get_user_data(resource);
	struct mango_xdg_toplevel_state *state;
	bool restore_requested = false;

	if (!mango_xdg_session_validate_name(resource, name)) {
		return;
	}

	if (!session || session->inert || !session->store) {
		(void)mango_xdg_toplevel_session_create(client, resource, id, NULL, NULL,
												 true);
		return;
	}

	if (mango_xdg_toplevel_state_find_by_resource(toplevel)) {
		wl_resource_post_error(
			resource, MANGO_XDG_SESSION_ERROR_ALREADY_ADDED,
			"xdg_toplevel has already been added to a session");
		return;
	}

	state = mango_xdg_toplevel_state_find_by_name(session->store, name);
	if (!state) {
		state = mango_xdg_toplevel_state_create(session->store, name);
		if (!state) {
			wl_resource_post_no_memory(resource);
			return;
		}
	} else {
		restore_requested = true;
		if (state->bound_toplevel && state->bound_toplevel != toplevel) {
			wl_resource_post_error(
				resource, MANGO_XDG_SESSION_ERROR_NAME_IN_USE,
				"toplevel name '%s' is already bound to a live xdg_toplevel", name);
			return;
		}
	}

	if (!mango_xdg_toplevel_state_bind_resource(state, toplevel)) {
		wl_resource_post_error(
			resource, MANGO_XDG_SESSION_ERROR_ALREADY_ADDED,
			"xdg_toplevel is already bound to another session entry");
		return;
	}

	/*
	 * Deliberately defer xdg_toplevel_session_v1.restored until Mango can
	 * replay real toplevel state during the initial configure sequence.
	 */
	if (!mango_xdg_toplevel_session_create(client, resource, id, session, state,
										   restore_requested) &&
		!restore_requested) {
		mango_xdg_toplevel_state_destroy(state);
	}
}

static void mango_xdg_session_handle_remove_toplevel(
	struct wl_client *client, struct wl_resource *resource, const char *name) {
	struct mango_xdg_session_resource *session =
		wl_resource_get_user_data(resource);
	struct mango_xdg_toplevel_state *state;

	(void)client;
	if (!mango_xdg_session_validate_name(resource, name)) {
		return;
	}

	if (!session || session->inert || !session->store) {
		return;
	}

	state = mango_xdg_toplevel_state_find_by_name(session->store, name);
	if (!state) {
		return;
	}

	mango_xdg_toplevel_state_destroy(state);
}

static void mango_xdg_toplevel_session_handle_destroy(
	struct wl_client *client, struct wl_resource *resource) {
	(void)client;
	wl_resource_destroy(resource);
}

static void mango_xdg_toplevel_session_handle_rename(
	struct wl_client *client, struct wl_resource *resource, const char *name) {
	struct mango_xdg_toplevel_session *toplevel_session =
		wl_resource_get_user_data(resource);
	struct mango_xdg_toplevel_state *existing;
	char *renamed;

	(void)client;
	if (!mango_xdg_session_validate_name(resource, name)) {
		return;
	}

	if (!toplevel_session || toplevel_session->inert || !toplevel_session->state ||
		!toplevel_session->state->store) {
		return;
	}

	existing = mango_xdg_toplevel_state_find_by_name(
		toplevel_session->state->store, name);
	if (existing && existing != toplevel_session->state) {
		wl_resource_post_error(
			resource, MANGO_XDG_SESSION_ERROR_NAME_IN_USE,
			"toplevel name '%s' is already in use in this session", name);
		return;
	}

	renamed = strdup(name);
	if (!renamed) {
		wl_resource_post_no_memory(resource);
		return;
	}

	free(toplevel_session->state->name);
	toplevel_session->state->name = renamed;
}
#endif

void mango_xdg_session_manager_init(struct wl_display *display) {
	(void)display;

#if MANGO_HAS_XDG_SESSION_MANAGEMENT_PROTOCOL
	if (xdg_session_manager.initialized) {
		return;
	}

	xdg_session_manager.display = display;
	xdg_session_manager.next_session_serial = 1;
	wl_list_init(&xdg_session_manager.sessions);
	xdg_session_manager.global = wl_global_create(
		display, &xdg_session_manager_v1_interface, 1, NULL,
		mango_xdg_session_manager_bind);
	if (!xdg_session_manager.global) {
		wlr_log(WLR_ERROR,
				"failed to create xdg-session-management-v1 global");
		return;
	}

	xdg_session_manager.initialized = true;
	wlr_log(WLR_DEBUG, "xdg-session-management-v1 global initialized");
#else
	wlr_log(WLR_DEBUG,
			"xdg-session-management-v1 protocol XML not available at build time");
#endif
}

void mango_xdg_session_manager_finish(void) {
#if MANGO_HAS_XDG_SESSION_MANAGEMENT_PROTOCOL
	struct mango_xdg_session_store *store, *tmp;

	if (!xdg_session_manager.initialized) {
		return;
	}

	if (xdg_session_manager.global) {
		wl_global_destroy(xdg_session_manager.global);
		xdg_session_manager.global = NULL;
	}

	wl_list_for_each_safe(store, tmp, &xdg_session_manager.sessions, link) {
		mango_xdg_session_store_destroy(store);
	}

	xdg_session_manager.display = NULL;
	xdg_session_manager.initialized = false;
#endif
}
