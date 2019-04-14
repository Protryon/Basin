/*
 * tools.h
 *
 *  Created on: Dec 26, 2016
 *      Author: root
 */

#ifndef TOOLS_H_
#define TOOLS_H_

#include <basin/item.h>
#include <basin/world.h>

struct tool_proficiency {
		uint16_t* proficient;
		size_t proficient_size;
};

struct tool_info {
		char* name;
		struct tool_proficiency* proficiencies;
		size_t proficient_size;
};

void tools_init();

struct tool_info* tools_get(char* name);

int tools_proficient(struct tool_info* info, uint8_t harvest_level, uint16_t b);

void tools_add(struct tool_info* tool);

#endif /* TOOLS_H_ */