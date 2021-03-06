/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/**
 * $Id$
 *
 * @file unlang/call.c
 * @brief Unlang "call" keyword evaluation.  Used for calling virtual servers.
 *
 * @copyright 2006-2019 The FreeRADIUS server project
 */
RCSID("$Id$")

#include "unlang_priv.h"

static unlang_action_t unlang_call(REQUEST *request,
				   UNUSED rlm_rcode_t *presult, UNUSED int *priority)
{
	unlang_stack_t		*stack = request->stack;
	unlang_stack_frame_t	*frame = &stack->frame[stack->depth];
	unlang_t		*instruction = frame->instruction;
	unlang_group_t		*g;
	int			indent;
	fr_io_final_t		final;
	unlang_stack_t		*current;
	CONF_SECTION		*old_server_cs;
	fr_io_process_t		old_process;
	void			*old_process_inst;

	g = unlang_generic_to_group(instruction);
	rad_assert(g->children != NULL);

	/*
	 *	@todo - allow for other process functions.  Mostly
	 *	because we need to save and resume this function, and
	 *	we haven't bothered to do that so far.
	 *
	 *	If we DO allow other functions, we need to replace
	 *	request->async->listener, as we want to pretend this
	 *	is a virtual request which didn't come in from the
	 *	network.  i.e. the other virtual server shouldn't be
	 *	able to access request->async->listener, and muck with
	 *	it's statistics, see it's configuration, etc.
	 */
	rad_assert(request->async->process == unlang_io_process_interpret);

	/*
	 *	@todo - We probably want to just remove the 'stack'
	 *	parameter from the interpreter function arguments.
	 *	It's not needed there.
	 */
	rad_assert(stack == request->stack);

	indent = request->log.unlang_indent;
	request->log.unlang_indent = 0; /* the process function expects this */

	current = request->stack;
	request->stack = talloc_zero(request, unlang_stack_t);

	old_server_cs = request->server_cs;
	old_process = request->async->process;
	old_process_inst = request->async->process_inst;

	request->server_cs = g->server_cs;
	request->async->process = g->process;
	request->async->process_inst = g->process_inst;

	RDEBUG("server %s {", cf_section_name2(g->server_cs));

	/*
	 *	@todo - we can't change packet types
	 *	(e.g. Access-Request -> Accounting-Request) unless
	 *	we're in a subrequest.
	 */
	final = request->async->process(request->async->process_inst, request, FR_IO_ACTION_RUN);

	RDEBUG("} # server %s", cf_section_name2(g->server_cs));

	/*
	 *	All other return codes are semantically equivalent for
	 *	our purposes.  "DONE" means "stopped without reply",
	 *	and REPLY means "finished successfully".  Neither of
	 *	those map well into module rcodes.  Instead, we rely
	 *	on the caller to look at request->reply->code.
	 */
	if (final == FR_IO_YIELD) {
		RDEBUG2("No yield for you!");
	}

	/*
	 *	@todo - save these in a resume state somewhere...
	 */
	request->log.unlang_indent = indent;
	talloc_free(request->stack);
	request->stack = current;

	request->server_cs = old_server_cs;
	request->async->process = old_process;
	request->async->process_inst = old_process_inst;

	RDEBUG("Continuing with contents of %s { ...", instruction->debug_name);

	/*
	 *	And then call the children to process the answer.
	 */
	unlang_interpret_push(stack, g->children, frame->result, UNLANG_NEXT_SIBLING, UNLANG_SUB_FRAME);
	return UNLANG_ACTION_PUSHED_CHILD;
}

void unlang_call_init(void)
{
	unlang_register(UNLANG_TYPE_CALL,
			   &(unlang_op_t){
				.name = "call",
				.func = unlang_call,
				.debug_braces = true
			   });
}

