// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define BIT(n) (1U << (n))
struct regmap { char name; };
struct event { char map, op; unsigned int reg, mask, value; };
static const struct event *expected;
static size_t index_, fail_at;

static int check(struct regmap *map, char op, unsigned reg, unsigned mask, unsigned value)
{
	const struct event *e = &expected[index_++];
	assert(map->name == e->map && op == e->op && reg == e->reg);
	assert(e->mask == mask && e->value == value);
	return index_ == fail_at ? -1 : 0;
}

#define regmap_write(m,r,v) check(m,'W',r,v,v)
#define regmap_set_bits(m,r,v) check(m,'S',r,v,v)
#define regmap_clear_bits(m,r,v) check(m,'C',r,v,0)
#define regmap_update_bits(m,r,k,v) check(m,'U',r,k,v)
/* Exercise the polling predicate with unrelated bits present and partial ACKs. */
#define regmap_read_poll_timeout(m,r,v,p,delay,timeout) ({ \
	struct event e = expected[index_]; \
	v = 0xa5355a5aU & ~e.mask; \
	v |= e.value; assert(p); \
	v ^= e.mask & (0U - e.mask); assert(!(p)); \
	assert(delay == 10 && timeout == 1000000); \
	check(m, 'P', r, e.mask, e.value); })

#include "cmf-md-sequence.h"

#define PROTECT(m,base,mask,set) \
	{m,'W',base+(set?4:8),mask,mask}, \
	{m,'P',base,mask,set?mask:0}, {m,'P',base+12,mask,set?mask:0}
static const struct event off[] = {
	PROTECT('I',0xc50,0x200,1), PROTECT('I',0xc40,0x800,1),
	PROTECT('E',0x80,0xc0,1),
	{'S','U',0xe00,4,0}, {'S','P',0xe00,0x40000000,0}, {'S','S',0xf24,3,3},
};
static const struct event on[] = {
	{'S','C',0xf24,3,0}, {'S','U',0xe00,4,4}, {'S','P',0xe00,0x40000000,0x40000000},
	PROTECT('E',0x80,0xc0,0), PROTECT('I',0xc40,0x800,0), PROTECT('I',0xc50,0x200,0),
};

int main(void)
{
	struct regmap spm = {'S'}, infra = {'I'}, emi = {'E'};
	struct cmf_md_maps maps = {&spm, &infra, &emi};
	for (int up = 0; up <= 1; up++) {
		size_t count = up ? sizeof(on)/sizeof(*on) : sizeof(off)/sizeof(*off);
		expected = up ? on : off;
		for (fail_at = 0; fail_at <= count; fail_at++) {
			index_ = 0;
			assert(cmf_md_switch(&maps, up) == (fail_at ? -1 : 0));
			assert(index_ == (fail_at ? fail_at : count));
		}
		printf("MD %s passed ordered trace and %zu injected failures\n", up ? "on" : "off", count);
	}
}
