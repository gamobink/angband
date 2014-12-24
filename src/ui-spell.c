/*
 * File: ui-spell.c
 * Purpose: Spell UI handing
 *
 * Copyright (c) 2010 Andi Sidwell
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */
#include "angband.h"
#include "cave.h"
#include "cmds.h"
#include "cmd-core.h"
#include "obj-tval.h"
#include "obj-ui.h"
#include "obj-util.h"
#include "object.h"
#include "player-spell.h"
#include "ui.h"
#include "ui-menu.h"




/**
 * Spell menu data struct
 */
struct spell_menu_data {
	int *spells;
	int n_spells;

	bool browse;
	bool (*is_valid)(int spell);
	bool show_description;

	int selected_spell;
};


/**
 * Is item oid valid?
 */
static int spell_menu_valid(struct menu *m, int oid)
{
	struct spell_menu_data *d = menu_priv(m);
	int *spells = d->spells;

	return d->is_valid(spells[oid]);
}

/**
 * Display a row of the spell menu
 */
static void spell_menu_display(struct menu *m, int oid, bool cursor,
		int row, int col, int wid)
{
	struct spell_menu_data *d = menu_priv(m);
	int spell = d->spells[oid];
	const class_spell *s_ptr = spell_by_index(spell);

	char help[30];
	char out[80];

	int attr;
	const char *illegible = NULL;
	const char *comment = NULL;

	if (s_ptr->slevel >= 99) {
		illegible = "(illegible)";
		attr = TERM_L_DARK;
	} else if (player->spell_flags[spell] & PY_SPELL_FORGOTTEN) {
		comment = " forgotten";
		attr = TERM_YELLOW;
	} else if (player->spell_flags[spell] & PY_SPELL_LEARNED) {
		if (player->spell_flags[spell] & PY_SPELL_WORKED) {
			/* Get extra info */
			get_spell_info(spell, help, sizeof(help));
			comment = help;
			attr = TERM_WHITE;
		} else {
			comment = " untried";
			attr = TERM_L_GREEN;
		}
	} else if (s_ptr->slevel <= player->lev) {
		comment = " unknown";
		attr = TERM_L_BLUE;
	} else {
		comment = " difficult";
		attr = TERM_RED;
	}

	/* Dump the spell --(-- */
	strnfmt(out, sizeof(out), "%-30s%2d %4d %3d%%%s", s_ptr->name,
			s_ptr->slevel, s_ptr->smana, spell_chance(spell), comment);
	c_prt(attr, illegible ? illegible : out, row, col);
}

/**
 * Handle an event on a menu row.
 */
static bool spell_menu_handler(struct menu *m, const ui_event *e, int oid)
{
	struct spell_menu_data *d = menu_priv(m);

	if (e->type == EVT_SELECT) {
		d->selected_spell = d->spells[oid];
		return d->browse ? TRUE : FALSE;
	}
	else if (e->type == EVT_KBRD) {
		if (e->key.code == '?') {
			d->show_description = !d->show_description;
		}
	}

	return FALSE;
}

/**
 * Show spell long description when browsing
 */
static void spell_menu_browser(int oid, void *data, const region *loc)
{
	struct spell_menu_data *d = data;
	int spell = d->spells[oid];

	if (d->show_description) {
		/* Redirect output to the screen */
		text_out_hook = text_out_to_screen;
		text_out_wrap = 0;
		text_out_indent = loc->col - 1;
		text_out_pad = 1;

		Term_gotoxy(loc->col, loc->row + loc->page_rows);
		text_out("\n%s\n", spell_by_index(spell)->text);

		/* XXX */
		text_out_pad = 0;
		text_out_indent = 0;
	}
}

static const menu_iter spell_menu_iter = {
	NULL,	/* get_tag = NULL, just use lowercase selections */
	spell_menu_valid,
	spell_menu_display,
	spell_menu_handler,
	NULL	/* no resize hook */
};

/** Create and initialise a spell menu, given an object and a validity hook */
static struct menu *spell_menu_new(const object_type *o_ptr,
		bool (*is_valid)(int spell))
{
	struct menu *m = menu_new(MN_SKIN_SCROLL, &spell_menu_iter);
	struct spell_menu_data *d = mem_alloc(sizeof *d);

	region loc = { -60, 1, 60, -99 };

	/* collect spells from object */
	d->n_spells = spell_collect_from_book(o_ptr, &d->spells);
	if (d->n_spells == 0 || !spell_okay_list(is_valid, d->spells, d->n_spells))
	{
		mem_free(m);
		mem_free(d);
		return NULL;
	}

	/* copy across private data */
	d->is_valid = is_valid;
	d->selected_spell = -1;
	d->browse = FALSE;
	d->show_description = FALSE;

	menu_setpriv(m, d->n_spells, d);

	/* set flags */
	m->header = "Name                             Lv Mana Fail Info";
	m->flags = MN_CASELESS_TAGS;
	m->selections = lower_case;
	m->browse_hook = spell_menu_browser;
	m->cmd_keys = "?";

	/* set size */
	loc.page_rows = d->n_spells + 1;
	menu_layout(m, &loc);

	return m;
}

/** Clean up a spell menu instance */
static void spell_menu_destroy(struct menu *m)
{
	struct spell_menu_data *d = menu_priv(m);
	mem_free(d);
	mem_free(m);
}

/**
 * Run the spell menu to select a spell.
 */
static int spell_menu_select(struct menu *m, const char *noun, const char *verb)
{
	struct spell_menu_data *d = menu_priv(m);
	char buf[80];

	screen_save();
	region_erase_bordered(&m->active);

	/* Format, capitalise and display */
	strnfmt(buf, sizeof buf, "%s which %s? ('?' to toggle description)", verb, noun);
	my_strcap(buf);
	prt(buf, 0, 0);

	menu_select(m, 0, TRUE);
	screen_load();

	return d->selected_spell;
}

/**
 * Run the spell menu, without selections.
 */
static void spell_menu_browse(struct menu *m, const char *noun)
{
	struct spell_menu_data *d = menu_priv(m);

	screen_save();

	region_erase_bordered(&m->active);
	prt(format("Browsing %ss. ('?' to toggle description)", noun), 0, 0);

	d->browse = TRUE;
	menu_select(m, 0, TRUE);

	screen_load();
}

/**
 * Browse a given book.
 */
void textui_book_browse(const object_type *o_ptr)
{
	struct menu *m;
	const char *noun = player->class->magic.spell_realm->spell_noun;

	m = spell_menu_new(o_ptr, spell_okay_to_browse);
	if (m) {
		spell_menu_browse(m, noun);
		spell_menu_destroy(m);
	} else {
		msg("You cannot browse that.");
	}
}

/**
 * Browse the given book.
 */
void textui_spell_browse(void)
{
	struct object *obj;

	if (!get_item(&obj, "Browse which book? ",
			"You have no books that you can read.",
			CMD_BROWSE_SPELL, obj_can_browse, (USE_INVEN | USE_FLOOR | IS_HARMLESS)))
		return;

	/* Track the object kind */
	track_object(player->upkeep, obj);
	handle_stuff(player->upkeep);

	textui_book_browse(obj);
}

/**
 * Get a spell from specified book.
 */
int get_spell_from_book(const char *verb, struct object *book,
		const char *error, bool (*spell_filter)(int spell))
{
	const char *noun = player->class->magic.spell_realm->spell_noun;

	struct menu *m;

	track_object(player->upkeep, book);
	handle_stuff(player->upkeep);

	m = spell_menu_new(book, spell_filter);
	if (m) {
		int spell = spell_menu_select(m, noun, verb);
		spell_menu_destroy(m);
		return spell;
	}

	return -1;
}

/**
 * Get a spell from the player.
 */
int get_spell(const char *verb, item_tester book_filter,
		cmd_code cmd, const char *error, bool (*spell_filter)(int spell))
{
	char prompt[1024];
	struct object *book;

	/* Create prompt */
	strnfmt(prompt, sizeof prompt, "%s which book?", verb);
	my_strcap(prompt);

	if (!get_item(&book, prompt, error,
			cmd, book_filter, (USE_INVEN | USE_FLOOR)))
		return -1;

	return get_spell_from_book(verb, book, error, spell_filter);
}
