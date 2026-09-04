module;

#include <qcode/generation/generation_continue.h>
#include <qcode/generation/generation_controller.h>
#include <qcode/generation/generation_service.h>

export module qcode.generation;

export namespace qcode {
using ::qcode::GenerationContext;
using ::qcode::GenerationController;
using ::qcode::GenerationRequest;
using ::qcode::GenerationService;
using ::qcode::ascii_lower;
using ::qcode::kBuildContinueNudge;
using ::qcode::kBuildModeReminder;
using ::qcode::looks_like_task_completion;
using ::qcode::looks_like_task_stall;
using ::qcode::run_generation_with_bus;
using ::qcode::should_append_user_message;
using ::qcode::should_auto_continue_build;
}  // namespace qcode
