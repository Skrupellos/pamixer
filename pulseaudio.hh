#pragma once

/*
 * Copyright (C) 2011 Clément Démoulins <clement@archivel.fr>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <string>
#include <list>
#include <cmath>

#include <pulse/pulseaudio.h>

enum state {
    CONNECTING,
    CONNECTED,
    ERROR
};
typedef enum state state_t;

void state_cb(pa_context* context, void* raw);
void sink_list_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata);
void server_info_cb(pa_context* context, const pa_server_info* i, void* raw);
void success_cb(pa_context* context, int success, void* raw);

double round(double value) {
    return (value > 0.0) ? floor(value + 0.5) : ceil(value - 0.5);
}

class Sink {
public:
    uint32_t index;
    std::string name;
    std::string description;
    pa_cvolume volume;
    int volume_percent;
    bool mute;

    Sink(const pa_sink_info* i) {
        index = i->index;
        name = i->name;
        description = i->description;
        volume.channels = i->volume.channels;
        int n;
        for (n = 0; n < volume.channels; ++n)
            volume.values[n] = i->volume.values[n];
        volume_percent = (int) round(((double) pa_cvolume_avg(&volume) * 100.) / PA_VOLUME_NORM);

        mute = i->mute == 1;
    }
};

class Pulseaudio {
private:
    pa_mainloop* mainloop;
    pa_mainloop_api* mainloop_api;
    pa_context* context;
    int retval;

    void iterate(pa_operation* op) {
        while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
            pa_mainloop_iterate(mainloop, 1, &retval);
        }
    }

public:
    state_t state;

    Pulseaudio(std::string client_name) {
        mainloop = pa_mainloop_new();
        mainloop_api = pa_mainloop_get_api(mainloop);
        context = pa_context_new(mainloop_api, client_name.c_str());
        pa_context_set_state_callback(context, &state_cb, this);

        state = CONNECTING;
        pa_context_connect(context, NULL, PA_CONTEXT_NOFLAGS, NULL);
        while (state == CONNECTING) {
            pa_mainloop_iterate(mainloop, 1, &retval);
        }
        if (state == ERROR) {
            throw "Connection error\n";
        }
    }

    ~Pulseaudio() {
        if (state == CONNECTED)
            pa_context_disconnect(context);
        pa_mainloop_free(mainloop);
    }

    std::list<Sink> get_sinks() {
        std::list<Sink> sinks;
        pa_operation* op = pa_context_get_sink_info_list(context, &sink_list_cb, &sinks);
        iterate(op);
        pa_operation_unref(op);

        return sinks;
    }

    Sink get_sink(uint32_t index) {
        std::list<Sink> sinks;
        pa_operation* op = pa_context_get_sink_info_by_index(context, index, &sink_list_cb, &sinks);
        iterate(op);
        pa_operation_unref(op);

        if (sinks.empty()) throw "The sink doesn't exit\n";
        return *(sinks.begin());
    }

    Sink get_sink(std::string name) {
        std::list<Sink> sinks;
        pa_operation* op = pa_context_get_sink_info_by_name(context, name.c_str(), &sink_list_cb, &sinks);
        iterate(op);
        pa_operation_unref(op);

        if (sinks.empty()) throw "The sink doesn't exit\n";
        return *(sinks.begin());
    }

    Sink get_default_sink() {
        std::string default_sink_name;
        pa_operation* op = pa_context_get_server_info(context, &server_info_cb, &default_sink_name);
        iterate(op);
        pa_operation_unref(op);

        return get_sink(default_sink_name);
    }

    void set_sink_volume(Sink& sink, int new_volume) {
        pa_cvolume* new_cvolume = pa_cvolume_set(&sink.volume, sink.volume.channels, (pa_volume_t) round(fmax(((double)new_volume * PA_VOLUME_NORM) / 100, 0)));
        pa_operation* op = pa_context_set_sink_volume_by_index(context, sink.index, new_cvolume, success_cb, NULL);
        iterate(op);
        pa_operation_unref(op);
    }

    void set_sink_mute(Sink& sink, bool mute) {
        pa_operation* op = pa_context_set_sink_mute_by_index(context, sink.index, (int) mute, success_cb, NULL);
        iterate(op);
        pa_operation_unref(op);
    }
};


/////////////////////////////////////////////////
// Callback functions

void state_cb(pa_context* context, void* raw) {
    Pulseaudio* pulse = (Pulseaudio*) raw;
    switch(pa_context_get_state(context))
    {
        case PA_CONTEXT_READY:
            pulse->state = CONNECTED;
            break;
        case PA_CONTEXT_FAILED:
            pulse->state = ERROR;
            break;
        case PA_CONTEXT_UNCONNECTED:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_TERMINATED:
            break;
    }
}

void sink_list_cb(pa_context *c, const pa_sink_info *i, int eol, void *raw) {
    if (eol != 0) return;

    std::list<Sink>* sinks = (std::list<Sink>*) raw;
    Sink s(i);
    sinks->push_back(s);
}

void server_info_cb(pa_context* context, const pa_server_info* i, void* raw) {
    std::string* default_sink_name = (std::string*) raw;
    *default_sink_name = i->default_sink_name;
}

void success_cb(pa_context* context, int success, void* raw) {
}

/////////////////////////////////////////////////
