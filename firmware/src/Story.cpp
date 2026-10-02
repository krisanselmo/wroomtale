#include "Story.h"
#include "Target.h"

namespace {
struct Node {
	char id[16];
	String audio;
	int8_t next[3];  // index into g_nodes, -1 when the button leads nowhere
	int8_t cond[3];  // flag index the choice requires, -1 when unconditional
	int8_t sets;     // flag index this node grants, -1 for none
};

Node g_nodes[Story::MAX_NODES];
uint8_t g_count = 0;
int8_t g_current = -1;

int8_t findNode(const char *id) {
	for (uint8_t i = 0; i < g_count; i++) {
		if (strcmp(g_nodes[i].id, id) == 0) return (int8_t)i;
	}
	return -1;
}

// Resolved after parsing, so a node may point forward.
char g_pending[Story::MAX_NODES][3][16];

// A node can grant a flag, a choice can require one.
char g_flagNames[Story::MAX_FLAGS][16];
uint8_t g_flagCount = 0;
uint16_t g_flags = 0;

int8_t flagIndex(const String &name) {
	for (uint8_t i = 0; i < g_flagCount; i++) {
		if (name == g_flagNames[i]) return (int8_t)i;
	}
	if (g_flagCount >= Story::MAX_FLAGS) {
		log_e("story: more than %u flags", Story::MAX_FLAGS);
		return -1;
	}
	strlcpy(g_flagNames[g_flagCount], name.c_str(), 16);
	return (int8_t)g_flagCount++;
}

void reset() {
	g_count = 0;
	g_current = -1;
	g_flagCount = 0;
	g_flags = 0;
	memset(g_pending, 0, sizeof(g_pending));
}

uint8_t buttonFromKey(const String &key) {
	if (key == "prev") return Story::BTN_PREV;
	if (key == "play") return Story::BTN_PLAY;
	if (key == "next") return Story::BTN_NEXT;
	return 0xFF;
}

// "id audio.mp3 prev=x play=y next=z", # starts a comment.
bool parseLine(String line) {
	const int hash = line.indexOf('#');
	if (hash >= 0) line = line.substring(0, hash);
	line.trim();
	if (line.isEmpty()) return true;

	if (g_count >= Story::MAX_NODES) {
		log_e("story: more than %u nodes", Story::MAX_NODES);
		return false;
	}

	Node &n = g_nodes[g_count];
	n = Node();
	n.next[0] = n.next[1] = n.next[2] = -1;
	n.cond[0] = n.cond[1] = n.cond[2] = -1;
	n.sets = -1;

	uint8_t field = 0;
	int from = 0;
	while (from <= line.length()) {
		int sp = line.indexOf(' ', from);
		if (sp < 0) sp = line.length();
		const String tok = line.substring(from, sp);
		from = sp + 1;
		if (tok.isEmpty()) continue;

		if (field == 0) strlcpy(n.id, tok.c_str(), sizeof(n.id));
		else if (field == 1) n.audio = tok;
		else {
			const int eq = tok.indexOf('=');
			if (eq < 0) { log_w("story: ignoring \"%s\"", tok.c_str()); continue; }
			const String key = tok.substring(0, eq);
			String value = tok.substring(eq + 1);

			if (key == "set") { n.sets = flagIndex(value); field++; continue; }

			const uint8_t b = buttonFromKey(key);
			if (b == 0xFF) { log_w("story: unknown key \"%s\"", tok.c_str()); continue; }

			// "cle?porte": the branch appears once "cle" is granted.
			const int q = value.indexOf('?');
			if (q >= 0) {
				n.cond[b] = flagIndex(value.substring(0, q));
				value = value.substring(q + 1);
			}
			strlcpy(g_pending[g_count][b], value.c_str(), 16);
		}
		field++;
	}

	if (field < 2) {
		log_e("story: line needs an id and an audio file");
		return false;
	}
	g_count++;
	return true;
}

bool resolve() {
	for (uint8_t i = 0; i < g_count; i++) {
		for (uint8_t b = 0; b < 3; b++) {
			if (!g_pending[i][b][0]) continue;
			const int8_t target = findNode(g_pending[i][b]);
			if (target < 0) {
				log_e("story: \"%s\" points at unknown node \"%s\"", g_nodes[i].id, g_pending[i][b]);
				return false;
			}
			g_nodes[i].next[b] = target;
		}
	}
	return true;
}
} // namespace

bool Story::loadFromText(const String &text, const String &folder) {
	reset();

	bool ok = true;
	int from = 0;
	while (ok && from <= (int)text.length()) {
		int eol = text.indexOf('\n', from);
		if (eol < 0) eol = text.length();
		ok = parseLine(text.substring(from, eol));
		from = eol + 1;
	}

	if (!ok || !g_count || !resolve()) {
		reset();
		return false;
	}

	// Manifest paths are relative to the story folder.
	for (uint8_t i = 0; i < g_count; i++) {
		if (g_nodes[i].audio[0] == '/' || Target::isBuiltin(g_nodes[i].audio)) continue;
		g_nodes[i].audio = folder + "/" + g_nodes[i].audio;
	}

	g_current = 0;
	log_i("story: %u nodes, starting at \"%s\"", g_count, g_nodes[0].id);
	return true;
}

bool Story::loadDemo() {
	return loadFromText("depart builtin:boot prev=gare next=route\n"
	                    "gare builtin:boot set=billet prev=quai\n"
	                    "route builtin:boot next=depart prev=billet?quai\n"
	                    "quai builtin:boot\n",
	                    String());
}

void Story::stop() { reset(); }
bool Story::active() { return g_current >= 0; }

bool Story::atEnd() { return active() && choiceMask() == 0; }

String Story::currentAudio() { return active() ? g_nodes[g_current].audio : String(); }
String Story::currentId() { return active() ? String(g_nodes[g_current].id) : String(); }

uint8_t Story::choiceMask() {
	if (!active()) return 0;
	uint8_t mask = 0;
	for (uint8_t b = 0; b < 3; b++) {
		const Node &n = g_nodes[g_current];
		if (n.next[b] < 0) continue;
		if (n.cond[b] >= 0 && !(g_flags & (1 << n.cond[b]))) continue; // locked
		mask |= 1 << b;
	}
	return mask;
}

String Story::flagList() {
	String out;
	for (uint8_t i = 0; i < g_flagCount; i++) {
		if (!(g_flags & (1 << i))) continue;
		if (out.length()) out += ", ";
		out += g_flagNames[i];
	}
	return out;
}

bool Story::choose(uint8_t button) {
	if (!active() || button > 2) return false;
	const int8_t target = g_nodes[g_current].next[button];
	if (target < 0) return false;
	if (!(choiceMask() & (1 << button))) return false; // locked by a flag
	g_current = target;

	if (g_nodes[g_current].sets >= 0) {
		g_flags |= 1 << g_nodes[g_current].sets;
		log_i("story: -> \"%s\" (+%s)", g_nodes[g_current].id, g_flagNames[g_nodes[g_current].sets]);
	} else {
		log_i("story: -> \"%s\"", g_nodes[g_current].id);
	}
	return true;
}
