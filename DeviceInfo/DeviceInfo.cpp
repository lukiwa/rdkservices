/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2020 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "DeviceInfo.h"

namespace WPEFramework {
namespace Plugin {

    SERVICE_REGISTRATION(DeviceInfo, 1, 0);

    static Core::ProxyPoolType<Web::JSONBodyType<DeviceInfo::Data>> jsonResponseFactory(4);

    void DeviceInfo::TestImplementation()
    {
        TRACE_L1("IMPLEMENTATION TEST");

        Exchange::IDeviceCapabilities::IOutputResolutionIterator* resolutionIterator = nullptr;
        Exchange::IDeviceCapabilities::OutputResolution resolution;

        if (_implementation->Resolutions(resolutionIterator) == Core::ERROR_NONE && resolutionIterator != nullptr) {
            while (resolutionIterator->Next(resolution)) {
                TRACE_L1("Resolution: %d", resolution);
            }
        }

        Exchange::IDeviceCapabilities::IAudioOutputIterator* audioIterator = nullptr;
        Exchange::IDeviceCapabilities::AudioOutput audio;

        if (_implementation->AudioOutputs(audioIterator) == Core::ERROR_NONE && audioIterator != nullptr) {
            while (audioIterator->Next(audio)) {
                TRACE_L1("Audio: %d", audio);
            }
        }

        Exchange::IDeviceCapabilities::IAudioOutputIterator* videoIterator = nullptr;
        Exchange::IDeviceCapabilities::AudioOutput video;

        if (_implementation->AudioOutputs(videoIterator) == Core::ERROR_NONE && videoIterator != nullptr) {
            while (videoIterator->Next(video)) {
                TRACE_L1("Video: %d", video);
            }
        }
        bool supportsAtmos = false;
        bool supportsHdr = false;
        bool supportsCec = false;
        _implementation->Atmos(supportsAtmos);
        _implementation->HDR(supportsHdr);
        _implementation->CEC(supportsCec);

        TRACE_L1("Supports Atmos: %s", supportsAtmos ? "true" : "false");
        TRACE_L1("Supports HDR: %s", supportsHdr ? "true" : "false");
        TRACE_L1("Supports CEC: %s", supportsCec ? "true" : "false");

        Exchange::IDeviceCapabilities::CopyProtection hdcp;
        _implementation->HDCP(hdcp);
        TRACE_L1("HDCP: %d", hdcp);
    }

    /* virtual */ const string DeviceInfo::Initialize(PluginHost::IShell* service)
    {
        TRACE_L1(_T("Init method"));

        ASSERT(_service == nullptr);
        ASSERT(_subSystem == nullptr);
        ASSERT(_implementation == nullptr);
        ASSERT(service != nullptr);

        Config config;
        config.FromString(service->ConfigLine());
        _skipURL = static_cast<uint8_t>(service->WebPrefix().length());
        _subSystem = service->SubSystems();
        _service = service;
        _systemId = Core::SystemInfo::Instance().Id(Core::SystemInfo::Instance().RawDeviceId(), ~0);

        _implementation = _service->Root<Exchange::IDeviceCapabilities>(_connectionId, 2000, _T("DeviceInfoImplementation"));

        if (_implementation == nullptr) {
            _service = nullptr;
            TRACE_L1(_T("DeviceInfo could not be instantiated"));
        } else {
            _implementation->Configure(_service);
        }

        ASSERT(_subSystem != nullptr);

        TestImplementation();
        // On success return empty, to indicate there is no error text.
        return (_subSystem != nullptr) ? EMPTY_STRING : _T("Could not retrieve System Information.");
    }

    /* virtual */ void DeviceInfo::Deinitialize(PluginHost::IShell* service)
    {
        ASSERT(_service == service);
        ASSERT(_implementation != nullptr);

        _implementation->Release();

        if (_connectionId != 0) {
            RPC::IRemoteConnection* connection(_service->RemoteConnection(_connectionId));
            // The process can disappear in the meantime...
            if (connection != nullptr) {
                // But if it did not dissapear in the meantime, forcefully terminate it. Shoot to kill :-)
                connection->Terminate();
                connection->Release();
            }
        }
        _service = nullptr;

        if (_subSystem != nullptr) {
            _subSystem->Release();
            _subSystem = nullptr;
        }

        _service = nullptr;
    }

    /* virtual */ string DeviceInfo::Information() const
    {
        // No additional info to report.
        return (string());
    }

    /* virtual */ void DeviceInfo::Inbound(Web::Request& /* request */)
    {
    }

    /* virtual */ Core::ProxyType<Web::Response> DeviceInfo::Process(const Web::Request& request)
    {
        ASSERT(_skipURL <= request.Path.length());

        Core::ProxyType<Web::Response> result(PluginHost::IFactories::Instance().Response());

        // By default, we assume everything works..
        result->ErrorCode = Web::STATUS_OK;
        result->Message = "OK";

        // <GET> - currently, only the GET command is supported, returning system info
        if (request.Verb == Web::Request::HTTP_GET) {

            Core::ProxyType<Web::JSONBodyType<Data>> response(jsonResponseFactory.Element());

            Core::TextSegmentIterator index(Core::TextFragment(request.Path, _skipURL, static_cast<uint32_t>(request.Path.length()) - _skipURL), false, '/');

            // Always skip the first one, it is an empty part because we start with a '/' if there are more parameters.
            index.Next();

            if (index.Next() == false) {
                AddressInfo(response->Addresses);
                SysInfo(response->SystemInfo);
                SocketPortInfo(response->Sockets);
            } else if (index.Current() == "Adresses") {
                AddressInfo(response->Addresses);
            } else if (index.Current() == "System") {
                SysInfo(response->SystemInfo);
            } else if (index.Current() == "Sockets") {
                SocketPortInfo(response->Sockets);
            }
            // TODO RB: I guess we should do something here to return other info (e.g. time) as well.

            result->ContentType = Web::MIMETypes::MIME_JSON;
            result->Body(Core::proxy_cast<Web::IBody>(response));
        } else {
            result->ErrorCode = Web::STATUS_BAD_REQUEST;
            result->Message = _T("Unsupported request for the [DeviceInfo] service.");
        }

        return result;
    }

    void DeviceInfo::SysInfo(JsonData::DeviceInfo::SysteminfoData& systemInfo) const
    {
        Core::SystemInfo& singleton(Core::SystemInfo::Instance());

        systemInfo.Time = Core::Time::Now().ToRFC1123(true);
        systemInfo.Version = _service->Version() + _T("#") + _subSystem->BuildTreeHash();
        systemInfo.Uptime = singleton.GetUpTime();
        systemInfo.Freeram = singleton.GetFreeRam();
        systemInfo.Totalram = singleton.GetTotalRam();
        systemInfo.Devicename = singleton.GetHostName();
        systemInfo.Cpuload = Core::NumberType<uint32_t>(static_cast<uint32_t>(singleton.GetCpuLoad())).Text();
        systemInfo.Serialnumber = _systemId;
    }

    void DeviceInfo::AddressInfo(Core::JSON::ArrayType<JsonData::DeviceInfo::AddressesData>& addressInfo) const
    {
        // Get the point of entry on WPEFramework..
        Core::AdapterIterator interfaces;

        while (interfaces.Next() == true) {

            JsonData::DeviceInfo::AddressesData newElement;
            newElement.Name = interfaces.Name();
            newElement.Mac = interfaces.MACAddress(':');
            JsonData::DeviceInfo::AddressesData& element(addressInfo.Add(newElement));

            // get an interface with a public IP address, then we will have a proper MAC address..
            Core::IPV4AddressIterator selectedNode(interfaces.IPV4Addresses());

            while (selectedNode.Next() == true) {
                Core::JSON::String nodeName;
                nodeName = selectedNode.Address().HostAddress();

                element.Ip.Add(nodeName);
            }
        }
    }

    void DeviceInfo::SocketPortInfo(JsonData::DeviceInfo::SocketinfoData& socketPortInfo) const
    {
        socketPortInfo.Runs = Core::ResourceMonitor::Instance().Runs();
    }

} // namespace Plugin
} // namespace WPEFramework
