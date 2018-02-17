#pragma once


#include <util/Types.h>
#include "ITask.h"

namespace nsfw {
    class NSFWImageTask : public ITask {
    public:
        NSFWImageTask(const TaskHeader &hdr) : header(hdr) {
            assert(header.GetType() == CheckNSFW);
        }

        NSFWImageTask(const TaskHeader &hdr, const std::vector<byte> &imageBytes) : header(hdr), rawImage(imageBytes) {
            assert(header.GetType() == CheckNSFW);
        }

        const std::vector<byte> &GetRawImage() const { return rawImage; }

    private:
        std::vector<byte> rawImage;
    };
}

