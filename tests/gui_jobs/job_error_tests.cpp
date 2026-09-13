#include "slic3r/GUI/Jobs/Job.hpp"

#include <wx/app.h>
#include <wx/init.h>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
class RecordingProgress final : public Slic3r::ProgressIndicator {
public:
    std::thread::id error_thread;
    std::vector<std::string> events;
    wxString message, description, extra;
    int error_code = 0;

    void show_error_info(wxString msg, int code, wxString desc, wxString detail) override
    {
        error_thread = std::this_thread::get_id();
        events.push_back("error");
        message = std::move(msg);
        error_code = code;
        description = std::move(desc);
        extra = std::move(detail);
    }
    void clear_percent() override {}
    void set_range(int) override {}
    void set_cancel_callback(CancelFn) override {}
    void set_progress(int) override { events.push_back("progress"); }
    void set_status_text(const char *) override {}
    int get_range() const override { return 100; }
};

class ErrorJob final : public Slic3r::GUI::Job {
public:
    using Job::Job;
    void report_from_worker()
    {
        // Local strings are destroyed before the main event loop runs.
        show_error_info(wxString::FromUTF8("Signature error — impressão"), -26,
                        wxString::FromUTF8("Descrição"), wxString::FromUTF8("详细信息"));
        update_status(50);
    }
private:
    void process() override {}
};

void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
}

int main()
{
    wxInitializer wx;
    if (!wx.IsOk()) {
        std::cerr << "Could not initialize wxWidgets\n";
        return 1;
    }

    try {
        auto progress = std::make_shared<RecordingProgress>();
        ErrorJob job(progress);
        std::thread worker([&job] { job.report_from_worker(); });
        worker.join();
        require(progress->events.empty(), "Worker touched the progress indicator before UI dispatch");

        wxAppConsole::GetInstance()->ProcessPendingEvents();
        require(progress->error_thread == std::this_thread::get_id(), "Error was not delivered on the main thread");
        require(progress->events == std::vector<std::string>{"error", "progress"}, "Error/status ordering changed");
        require(progress->message == wxString::FromUTF8("Signature error — impressão"), "Error message was lost");
        require(progress->error_code == -26, "Error code was lost");
        require(progress->description == wxString::FromUTF8("Descrição"), "Error description was lost");
        require(progress->extra == wxString::FromUTF8("详细信息"), "Error details were lost");

        auto discarded = std::make_shared<RecordingProgress>();
        {
            ErrorJob deleted_job(discarded);
            std::thread deleted_worker([&deleted_job] { deleted_job.report_from_worker(); });
            deleted_worker.join();
        }
        wxAppConsole::GetInstance()->ProcessPendingEvents();
        require(discarded->events.empty(), "Destroyed job left a pending progress callback");
        std::cout << "PASS: main-thread error delivery, UTF-8 payload, event ordering, and pending-event cleanup\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
