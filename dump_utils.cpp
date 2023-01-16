#include "dump_utils.hpp"

#include <fmt/core.h>

#include <phosphor-logging/log.hpp>

namespace phosphor
{
namespace dump
{

using namespace phosphor::logging;

std::string getService(sdbusplus::bus::bus& bus, const std::string& path,
                       const std::string& interface)
{
    constexpr auto objectMapperName = "xyz.openbmc_project.ObjectMapper";
    constexpr auto objectMapperPath = "/xyz/openbmc_project/object_mapper";

    auto method = bus.new_method_call(objectMapperName, objectMapperPath,
                                      objectMapperName, "GetObject");

    method.append(path);
    method.append(std::vector<std::string>({interface}));

    std::vector<std::pair<std::string, std::vector<std::string>>> response;

    try
    {
        auto reply = bus.call(method);
        reply.read(response);
        if (response.empty())
        {
            log<level::ERR>(fmt::format("Error in mapper response for getting "
                                        "service name, PATH({}), INTERFACE({})",
                                        path, interface)
                                .c_str());
            return std::string{};
        }
    }
    catch (const sdbusplus::exception::exception& e)
    {
        log<level::ERR>(fmt::format("Error in mapper method call, "
                                    "errormsg({}), PATH({}), INTERFACE({})",
                                    e.what(), path, interface)
                            .c_str());
        return std::string{};
    }
    return response[0].first;
}

BootProgress getBootProgress()
{
    BootProgress bootProgessStage;
    constexpr auto bootProgressInterface =
        "xyz.openbmc_project.State.Boot.Progress";
    // TODO Need to change host instance if multiple instead "0"
    constexpr auto hostStateObjPath = "/xyz/openbmc_project/state/host0";
    std::string value =
        getStateValue(bootProgressInterface, hostStateObjPath, "BootProgress");
    bootProgessStage = sdbusplus::xyz::openbmc_project::State::Boot::server::
        Progress::convertProgressStagesFromString(value);
    return bootProgessStage;
}

HostState getHostState()
{
    HostState hostState;
    constexpr auto hostStateInterface = "xyz.openbmc_project.State.Host";
    // TODO Need to change host instance if multiple instead "0"
    constexpr auto hostStateObjPath = "/xyz/openbmc_project/state/host0";
    std::string value =
        getStateValue(hostStateInterface, hostStateObjPath, "CurrentHostState");
    hostState = sdbusplus::xyz::openbmc_project::State::server::Host::
        convertHostStateFromString(value);
    return hostState;
}

std::string getStateValue(std::string intf, std::string objPath,
                          std::string state)
{
    std::string stateVal;
    try
    {
        auto bus = sdbusplus::bus::new_default();
        auto service = getService(bus, objPath, intf);

        auto method =
            bus.new_method_call(service.c_str(), objPath.c_str(),
                                "org.freedesktop.DBus.Properties", "Get");

        method.append(intf, state);

        auto reply = bus.call(method);

        using DBusValue_t =
            std::variant<std::string, bool, std::vector<uint8_t>,
                         std::vector<std::string>>;
        DBusValue_t propertyVal;

        reply.read(propertyVal);

        stateVal = std::get<std::string>(propertyVal);
    }
    catch (const sdbusplus::exception::exception& e)
    {
        log<level::ERR>(fmt::format("D-Bus call exception, OBJPATH({}), "
                                    "INTERFACE({}), PROPERRT({}) EXCEPTION({})",
                                    objPath, intf, state, e.what())
                            .c_str());
        throw std::runtime_error("Failed to get host state property");
    }
    catch (const std::bad_variant_access& e)
    {
        log<level::ERR>(
            fmt::format("Exception raised while read host state({}) property "
                        "value,  OBJPATH({}), INTERFACE({}), EXCEPTION({})",
                        state, objPath, intf, e.what())
                .c_str());
        throw std::runtime_error("Failed to get host state property");
    }

    return stateVal;
}

bool isHostRunning()
{
    // TODO #ibm-openbmc/dev/2858 Revisit the method for finding whether host
    // is running.
    BootProgress bootProgressStatus = phosphor::dump::getBootProgress();
    if ((bootProgressStatus == BootProgress::SystemInitComplete) ||
        (bootProgressStatus == BootProgress::SystemSetup) ||
        (bootProgressStatus == BootProgress::OSStart) ||
        (bootProgressStatus == BootProgress::OSRunning) ||
        (bootProgressStatus == BootProgress::PCIInit))
    {
        return true;
    }
    return false;
}

bool isHostQuiesced()
{
    HostState hostState = phosphor::dump::getHostState();
    if (hostState == HostState::Quiesced)
    {
        return true;
    }
    return false;
}

void createPEL(const std::string& dumpFilePath, const std::string& dumpFileType,
               const int dumpId, const std::string& pelSev,
               const std::string& errIntf)
{
    try
    {
        constexpr auto loggerObjectPath = "/xyz/openbmc_project/logging";
        constexpr auto loggerCreateInterface =
            "xyz.openbmc_project.Logging.Create";
        constexpr auto loggerService = "xyz.openbmc_project.Logging";
        constexpr auto dumpFileString = "File Name";
        constexpr auto dumpFileTypeString = "Dump Type";
        constexpr auto dumpIdString = "Dump ID";

        sd_bus* pBus = nullptr;
        sd_bus_default(&pBus);

        // Implies this is a call from Manager. Hence we need to make an async
        // call to avoid deadlock with Phosphor-logging.
        auto retVal = sd_bus_call_method_async(
            pBus, nullptr, loggerService, loggerObjectPath,
            loggerCreateInterface, "Create", nullptr, nullptr, "ssa{ss}",
            errIntf.c_str(), pelSev.c_str(), 3, dumpIdString,
            std::to_string(dumpId).c_str(), dumpFileString,
            dumpFilePath.c_str(), dumpFileTypeString, dumpFileType.c_str());

        if (retVal < 0)
        {
            log<level::ERR>("Error calling sd_bus_call_method_async",
                            entry("retVal=%d", retVal),
                            entry("MSG=%s", strerror(-retVal)));
        }
    }
    catch (const std::exception& e)
    {
        log<level::ERR>(
            "Error in calling creating PEL. Standard exception caught",
            entry("ERROR=%s", e.what()));
    }
}

} // namespace dump
} // namespace phosphor
