#include "NSFWImageChecker.h"

bool nsfw::NSFWImageChecker::IsAppropriateTask(std::shared_ptr<nsfw::ITask> &task) {
    return task.get()->GetType() == TaskType::CheckNSFW;
}

void nsfw::NSFWImageChecker::HandleTask(std::shared_ptr<nsfw::ITask> &task) {
    publisher.Send(task);
}
