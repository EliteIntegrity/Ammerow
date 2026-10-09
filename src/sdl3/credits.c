/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/credits.c
 * \brief Chronological credits presentation for the SDL3 home screen.
 *
 * The historical names and roles below are drawn from docs/thanks.rst and
 * inherited source history. Keep corrections in the contributor record and
 * this concise presentation in sync.
 *
 *
 */

#include "sdl3/credits.h"

#include "angband.h"
#include "buildid.h"
#include "sdl3/render-internal.h"
#include "sdl3/theme.h"
#include "sdl3/ui-draw.h"

#include <ctype.h>
#include <string.h>

struct sdl3_credit_entry {
	const char *era;
	const char *names;
	const char *work;
};

/* Cohort descriptions intentionally follow the granularity of the upstream
 * credits.  A name in a cohort is not assigned a more specific role than the
 * historical record supports. */
static const struct sdl3_credit_entry credit_entries[] = {
	{
		"1983-1985 - MORIA",
		"Robert Alan Koeneke",
		"Original design and implementation of Moria."
	}, {
		NULL,
		"Jimmey Wayne Todd Jr.",
		"Character generation and early Moria modules."
	}, {
		"1987-1990 - UMORIA",
		"James E. Wilson",
		"Unix/C development and stewardship of Umoria."
	}, {
		NULL,
		"D. G. Kneller, Christopher J. Stuart, Curtis McCauley, Stephen A. "
		"Jacobs, William Setzer, David J. Grabiner, Dan Bernstein",
		"Ports, recall/options/inventory/running code, object naming, testing, "
		"consistency work, and bug fixes for Umoria."
	}, {
		"1990-1994 - EARLY DEVELOPMENT OF THE SCOTTISH GAME",
		"Alex Cutler, Andy Astrand, Sean Marsh, Geoff Hill, Charles Teague, "
		"Charles Swiger",
		"Creation, expansion, porting, maintenance, integration, and releases "
		"through the 2.6.x line."
	}, {
		"1995-1999 - THE 2.7.x AND 2.8.x LINE",
		"Ben Harrison",
		"Major rewrite, portability architecture, interfaces, documentation, "
		"and maintenance."
	}, {
		"2000-2006 - THE 2.9.x AND 3.0.x LINE",
		"Robert Ruehlmann",
		"Maintenance, bug fixing, integration, and expansion of data-driven "
		"customisation."
	}, {
		"2007-2014 - THE 3.0.8 TO 3.5.1 LINE",
		"Anna Sidwell",
		"Maintenance and continued modernisation of the game and its code."
	}, {
		"BEFORE 4.0 - PATCHES, BUG FIXES, PORTS, CONTENT, AND OTHER WORK",
		"Peter Berger, Andrew Hill, Werner Baer, Tom Morton, Cyric the Mad, "
		"Chris Kern, Jurriaan Kalkman, Alexander Wilkins, Mauro Scarpa, facade, "
		"Dennis van Es, Kenneth A. Strom, Wei-Hwa Huang, Nikodemus, Timo "
		"Pietila, Shayne Steele, Dr. Andrew White, Greg Flint, Christopher "
		"Jeris, Ian Parkhouse, Warhammer, Scott Holder, Brent Ross, Kazuo Ito, "
		"Willem Siemelink, Luthien, David J. Grabiner, Ilya Bely, chungkuo, "
		"Kieron Dunbar, George W. Harris, Joseph Oberlander, Paul Moore, Andreas "
		"Tophinke, Leon Marrick, Peter J. Rowe, Wim Benthem, Jaroslav Sladek, "
		"Keith Perkins, Hugo Kornelis, Pete Mack, Marco K, Frank Palazzolo, "
		"Christer Nyfalt, Andrew Doull, Kenneth Boyd, Iain McFall, Christophe "
		"Cavalaria, Brendon Oliver, Zaxx, theninja, Twilight Forest, jbu, "
		"AnonymousHero, Stefan O'Rear, SilverD, Ed Graham, Tobias Franke, "
		"rhinocesaurus, Bron, Mangojuice, Chris Robertson, Joe Buck, tigen, Big "
		"Al, Paul Blay, J. D. White, Rowan Beentje, pelpel, Shanoah Alkire, "
		"Alexander Philips, mikon, Antoine, Irashtar, roustk, Diego Gonzalez, "
		"Takeshi Mogami, Julian Lighton, Aram Harrow, William Tanksley, Chris "
		"Ang, Dean Anderson, Daniel Nash, David Blackston, Heino Vander Sanden, "
		"Mark Kvale, Sheldon Simms, Topi Ylinen, Gileba, Jeff Greene, Joshua "
		"Middendorf, Tom Demuyt, Alexander Ulyanov, Alexander Malmberg, Chris "
		"R. Martin, Chris Herborth, Craig Oliver, DarkGod, David Boeren, David "
		"DeLaney, David Kahane, Dennis Payne, Desvignes Sebastien, Ekkehard "
		"Kraemer, Eugene Hung, HansJoachim Baader, Heiko Herold, John Rauser, "
		"Jonathan Sari, Joseph William Dixon, Joseph Hall, John M. Kewley, Ken "
		"Wigle, Keith H. Randall, Kevin Bracey, Mike Marcelais, Maarten "
		"Hazewinkel, Peter Ammon, Peter Seebach, Randy Hutson, Scott Egashira, "
		"Skirmantas Kligys, Steve Linberg, Silas Dunsmore, Tom Harris, Ron "
		"Anderson, Ross E. Becker, Denis Eropkin, Torbjorn Lindgren, Lars "
		"Haugseth, Jon Taylor, Roland Jay Roberts, Sergey, cb, Michael Pope, hmj, "
		"Colin Spry, Ed Cogburn, Yendor, Thomas Dedorson, Ewert, Rooslan S. "
		"Khayrov, Thapper, Max Stats, SSK, ChodTheWacko, Jonas Lith, Jens Schou, "
		"Lebannen, Daniel Santos, Edd Barrett (vext01), mtadd, Peter Denison "
		"(noz), Kiyoshi Aman (Aerdan), David Barr (david3x3x3), Chris Weisiger "
		"(Derakon), Buzzkill, Scott Michael, LastQuestion, danial.santos, "
		"LuthienCeleste, shadowsun",
		"The upstream record credits this cohort collectively; it does not "
		"assign a reliable individual role to every name."
	}, {
		"DOCUMENTED SPECIALIST CONTRIBUTIONS",
		"Greg Wooledge",
		"Autoconf support, the original random-artifact generator, balance "
		"ideas, and the revised spell list."
	}, {
		NULL, "Tim Baker",
		"The easy patch and organisation of patches for the 2.8.5 beta."
	}, {
		NULL, "Eytan Zweig", "Bug reports and patches."
	}, {
		NULL, "Jonathan Ellis",
		"Edit/help data, monsters, artefacts, vaults, objects, a player race, "
		"and extensive rebalancing."
	}, {
		NULL, "John I'anson-Holton", "Bug fixes and patches."
	}, {
		NULL, "Steven Fuerst", "X11, XAW, and GTK improvements."
	}, {
		NULL, "Bablos", "Amiga interface updates."
	}, {
		NULL, "Matthias Kurzke",
		"The ego-item patch and code changes for the JLE patch."
	}, {
		NULL, "Keldon Jones", "Improved monster AI."
	}, {
		NULL, "Adam Bolt", "Original 16x16 tiles."
	}, {
		NULL, "Arcum Dagsson", "Configurable artefact activations."
	}, {
		NULL, "Prfnoff",
		"Customisable player races, histories, shop owners, and related data."
	}, {
		NULL, "Mark Howson", "Amiga interface improvements."
	}, {
		NULL, "Musus Umbra", "Acorn RISC OS interface improvements."
	}, {
		NULL, "Hallvard B. Furuseth",
		"Autoconf improvements, code clean-up, and many bug fixes."
	}, {
		NULL, "Kusunose Toru", "Bug fixes."
	}, {
		NULL, "Eddie Grove",
		"Bug fixes, patches, design ideas, and identification-by-use."
	}, {
		NULL, "Nomad", "8x16 tiles and many room templates."
	}, {
		NULL, "Markus Oberhumer and Laszlo Molnar",
		"The UPX executable packer used by historical releases."
	}, {
		NULL, "qwerty", "LaTeX-based help generation."
	}, {
		NULL, "Federico Poloni (fph)",
		"Manual and documentation updates and reStructuredText formatting."
	}, {
		NULL, "Peter Ammon (ridiculous_fish)",
		"The rewritten macOS Cocoa interface."
	}, {
		NULL, "William Moore (MarbleDice)",
		"Bitflag code and many improvements during the 3.1.x line."
	}, {
		NULL, "Antony Sidwell (ajps)",
		"Default point-based stats, UI improvements, and the original "
		"core/UI split."
	}, {
		NULL, "PowerWyrm", "Many bug fixes and code improvements."
	}, {
		"THE SCOTTISH GAME 4.0.x - CODE",
		"Aaron Bader (fizzix), Antony Sidwell (ajps), Andi Sidwell (takkaria), "
		"Bardur Arantsson, Ben Semmler (molybdenum), Chris Carr (magnate), "
		"Christian Heckendorf, Elly Fong-Jones (elly), Elsairon, Erik Osheim "
		"(d_m), flaviommedeiros, Jagath Samarabandu, Jose Antonio Dura, Kevin J. "
		"Fletcher, LostTemplar, Michel Carroll, Nick McConnell, Nomad, Peter "
		"Denison (noz), phantom-voltage, PowerWyrm, redlumf, Robert Au (myshkin), "
		"Rydelfox, Timothy Collett",
		"Code contributions to the 4.0.x line."
	}, {
		"THE SCOTTISH GAME 4.0.x - TESTING AND REPORTING",
		"Ingwe Ingweron, Nomad, MattB, Thraalbeast, tumbleweed, AndyHK, Rhonwyn, "
		"Jungle_Boy, Darin, StMicah, debo, pen, topazg, wobbly, DeusIrae, Timo "
		"Pietila, ranger jeff, passer_by, Runaway1956, mrrstark, Estie, shreesh, "
		"elliptic, Gorbad, letslaugh, ShadowTechnology, bryan.g.hutchinson, "
		"Werbaer, fph, yyt16384, kandrc, Nivra, Tarrasque, Egavactip, zog, "
		"troycheek",
		"Beta testing, bug reporting, and fixes for the 4.0.x line."
	}, {
		"THE SCOTTISH GAME 4.1.x - CODE",
		"Alex Mooney, Andi Sidwell (takkaria), AndreyB, Bardur Arantsson, Ben "
		"Semmler, crayonsmelting, Derakon, Erik Osheim (d_m), fizzix (Aaron "
		"Bader), Flavio Medeiros, Graeme Russ, Gwilim Owen, Jean-Francois Caron, "
		"kaypy, Kevin J. Fletcher, Nomad, Pete McIlroy, Peter (Hermann Doppes), "
		"Peter McIlroy, phantom-voltage, PowerWyrm, rmzelle, rowanbeentje, Tiara "
		"Smith, Twisted Pair in my Hair, Vic K (t4nk), William Orr",
		"Code contributions to the 4.1.x line."
	}, {
		"THE SCOTTISH GAME 4.2.x - CODE",
		"Adam Goodman (agoodman), Adriankhl, Adrian Siekierka (asiekierka), "
		"Alberto Mardegan (mardy), Alex Mooney, Alexandre Detiste, Andre "
		"Maroneze, Andrew York, Anna Sidwell (takkaria), ArmiesAndCastles "
		"(v-chirkov), bacchist, Ben Collver, Ben Semmler (molybdenum), Bardur "
		"Arantsson, Bill Peterson, Binrui Dong, bron, Cameron Ball, Colin "
		"Woodbury, Cuboideb (Diego Gonzalez), Dag Arneson (sanedragon), Daniel "
		"Burgener, David Medley, Derakon, Diego Herrera, Eastwind921, edz314, "
		"Elly Fong-Jones (elly), Eric Branlund (backwardsEric), Erik Osheim "
		"(d_m), fizzix (Aaron Bader), floyza, fruviad, hardfau1t, jdholbrook81, "
		"jefetienne, Joan Andres, John Weismiller (emar), Jordan Philyaw "
		"(philyawj), Justin Chua, Justin Holbrook (jdholbrook81), Klaas van "
		"Aarsen, kleo, Kusunose Toru, Lars Haugseth, Lemon Rush (ThirdLemon), "
		"magnate (Chris Carr), MarbleDice, memmaker, Michael Courtney (wobbly), "
		"Mikolaj Konarski, Nima Hoda, Paul Johnson, pav1388, piels, PowerWyrm, "
		"pwinckles (moosferatu), Rodent/Sideways/sulkasormi, Ryan Schmidt, Sean "
		"Dewar (seandewar), Shanoah Alkire, spenserblack, Stefan Strogin, tangar, "
		"Tim Schumacher (timschumi), Tom Morton (tom), Tykhon Tarnavsky "
		"(tikhont), Vic K, wkmanire, wobbly, Yutao Yuan (infmagic2047)",
		"Code contributions to the 4.2.x line."
	}, {
		"THE SCOTTISH GAME 4.2.x - REPORTS, IDEAS, AND DISCUSSION",
		"Adam, Anarchic Fox, animal_waves, AnonymousHero, Anthon van der Neut "
		"(AvdN), Antoine, Aodhlin, archolewa, Aszazin, Atriel, bio_hazard, "
		"Bogatyr, bughunter, bunnies, cacheflood, Capn_Carpaccio, Carg, "
		"Carnivean, cccfire, Chud, cjslates, ClaytonAguiar, Clearshade, clouded, "
		"Cold_Heart, Combatereak, damerell, Darin, David Chmelik (dchmelik), "
		"debo (cyberdemons pls), Den XingJing (MicroMilo), desstorm, dionysian, "
		"Djbanete, dos350, Dragget, dr-kraemer-everett, drquicksilver, Ed_47569, "
		"Egavactip, emulord, EpicMan, Eric, Estie, Estragon, Evilpotatoe, ewert, "
		"Flambard, floatRand, FogSpear, fph, Gauss, Geoff Hill, geoff_tewierik, "
		"Gglibertine, Glorfindel, Goaticus, Grotug, gtrudeau88, Gwarl, half, "
		"HallucinationMushroom, HebrewToYou, Holy_Rage, Hounded, Hrrunstar, "
		"Huqhox, ImEsteban, Ingwe Ingweron, JBright, Jeff Greene (nppangband), "
		"jevansau, jml34, jsv, Julian (jl8e), juliusyang, Jungle_Boy, kandrc, "
		"kaypy, khearn, kineahora, Kinematics, lanactoor, lonadar, luneya, "
		"malcontent, Mark, MattB, Mike, misanthropope, MITZE, MKula, Mondkalb, "
		"Monkey Face, Moving Pictures, mrfy, Muscleguy, MWGE, Naranathu "
		"Bhranthan, Narry, NCountr, Netbrian, NightLizard, Nomad, olivertheorem, "
		"Once, Oraticus, Pahasusi, Patashu, Pete Mack, Philip, Pondlife, Pussy "
		"Galore, quarague, Quirk, Raerick, Raxmei, RegalStar, renato, "
		"robinjohnson, Rydel, Saru, Scatha, schatz, scrarth, Sebastian Jensen "
		"(gonX), Selkie, shirish, Sinquen, Sky, smbhax, spara, Sparrow the "
		"Dunadan, Sphara, superuser-does, swaggert, the Invisible Stalker, "
		"Sylvain frOOm (froom), Therem Harth, Thraalbee, Tibarius, Timo Pietila, "
		"TJA, TJS, topazg, Torr, Turmfalke2, tyreforhyred, Ugramoth, Vivit, "
		"Voovus, Vorczar, Werbaer, whartung, Whelk, will_asher, WindLord, "
		"Wiwaxia, Xaxyx, Youssarian, Zikke, Zirael",
		"Bug reports, suggested changes, and design discussion affecting 4.2.x."
	}, {
		"DESIGN ACKNOWLEDGEMENT",
		"Luke McConnell",
		"Long-running game-design conversations that influenced the breadth and "
		"depth of the inherited game."
	}, {
		"HISTORICAL UPSTREAM MEDIA AUTHORS",
		"Raymond 'Shockbolt' Gaustadnes, Adam Bolt, David Gervais, Dubtrain",
		"Authors of optional media distributed with upstream Angband. Ammerow "
		"does not use or ship those tile sheets or sound samples."
	}, {
		NULL,
		"Sam Lantinga and SDL contributors; SDL_ttf, SDL_image, and FreeType "
		"contributors",
		"Cross-platform windowing, input, audio, image, text, and font rendering."
	}, {
		NULL,
		"Roboto Mono, Cozette, VT323, IBM Plex Mono, Share Tech Mono",
		"Interface and map typefaces; their MIT and OFL notices apply."
	}, {
		VERSION_TRADEMARK_NAME,
		"Based on Angband, copyright Angband developers past and present; on "
		"Umoria, copyright 1989 James E. Wilson; and on Moria, copyright 1985 "
		"Robert Alan Koeneke.",
		"AMMEROW is a trade mark of John Horton. Full copyright, licence, "
		"source, and asset notices accompany the game."
	}
};

struct sdl3_credit_sink {
	struct sdl3_visual *visual;
	const struct sdl3_theme *theme;
	int left;
	int top;
	int width;
	int offset;
	int available;
	int line;
};

static void credit_line(struct sdl3_credit_sink *sink, const char *text,
		int length, int indent, SDL_Color color)
{
	if (sink->visual && sink->line >= sink->offset &&
			sink->line < sink->offset + sink->available && text && length > 0) {
		char buffer[512];
		int copy = SDL_min(length, (int)sizeof(buffer) - 1);

		memcpy(buffer, text, copy);
		buffer[copy] = '\0';
		sdl3_ui_draw_text(sink->visual, buffer, sink->left + indent,
			sink->top + sink->line - sink->offset,
			sink->width - indent, color);
	}
	sink->line++;
}

static void credit_blank(struct sdl3_credit_sink *sink)
{
	sink->line++;
}

static void credit_wrapped(struct sdl3_credit_sink *sink, const char *text,
		int indent, SDL_Color color)
{
	const char *cursor = text;
	int available = SDL_max(1, sink->width - indent);

	if (!text || !text[0]) return;
	while (*cursor) {
		const char *end;
		const char *break_at = NULL;
		int length = 0;

		while (*cursor && isspace((unsigned char)*cursor)) cursor++;
		if (!*cursor) break;
		end = cursor;
		while (*end && length < available) {
			if (isspace((unsigned char)*end)) break_at = end;
			end++;
			length++;
		}
		if (*end && break_at && break_at > cursor) end = break_at;
		while (end > cursor && isspace((unsigned char)end[-1])) end--;
		credit_line(sink, cursor, (int)(end - cursor), indent, color);
		cursor = end;
	}
}

static void visit_credits(struct sdl3_credit_sink *sink)
{
	int i;

	for (i = 0; i < (int)N_ELEMENTS(credit_entries); i++) {
		const struct sdl3_credit_entry *entry = &credit_entries[i];

		if (entry->era) {
			credit_wrapped(sink, entry->era, 0, sink->theme->title);
		}
		credit_wrapped(sink, entry->names, 2, sink->theme->accent);
		credit_wrapped(sink, entry->work, 4, sink->theme->text);
		if (i + 1 < (int)N_ELEMENTS(credit_entries)) credit_blank(sink);
	}
}

static int credit_line_count(int width)
{
	const struct sdl3_theme counting_theme = { 0 };
	struct sdl3_credit_sink sink = {
		NULL, &counting_theme, 0, 0, SDL_max(12, width), 0, 0, 0
	};

	visit_credits(&sink);
	return sink.line;
}

int sdl3_credits_max_offset(int width, int available_rows)
{
	return SDL_max(0, credit_line_count(width) - SDL_max(1, available_rows));
}

void sdl3_credits_draw(SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int left, int width, int offset)
{
	int top = 7;
	int available = SDL_max(1, visual->rows - top - 4);
	int maximum = sdl3_credits_max_offset(width, available);
	struct sdl3_credit_sink sink = {
		visual, theme, left, top, width, SDL_clamp(offset, 0, maximum),
		available, 0
	};
	char position[80];

	(void)renderer;
	sdl3_ui_draw_text(visual, "CREDITS", left, 3, width, theme->title);
	sdl3_ui_draw_text(visual,
		"A chronological record of the foundations beneath Ammerow.",
		left, 5, width, theme->muted);
	visit_credits(&sink);
	strnfmt(position, sizeof(position), "Up/Down scroll   %d / %d   Escape returns home",
		SDL_clamp(offset, 0, maximum) + 1, maximum + 1);
	sdl3_ui_draw_text(visual, position, left, visual->rows - 3, width,
		theme->muted);
}
