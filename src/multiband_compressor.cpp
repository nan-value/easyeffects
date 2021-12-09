/*
 *  Copyright © 2017-2022 Wellington Wallace
 *
 *  This file is part of EasyEffects.
 *
 *  EasyEffects is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  EasyEffects is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with EasyEffects.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "multiband_compressor.hpp"

MultibandCompressor::MultibandCompressor(const std::string& tag,
                                         const std::string& schema,
                                         const std::string& schema_path,
                                         PipeManager* pipe_manager)
    : PluginBase(tag, plugin_name::multiband_compressor, schema, schema_path, pipe_manager, true),
      lv2_wrapper(std::make_unique<lv2::Lv2Wrapper>("http://lsp-plug.in/plugins/lv2/sc_mb_compressor_stereo")) {
  if (!lv2_wrapper->found_plugin) {
    util::debug(log_tag + "http://lsp-plug.in/plugins/lv2/sc_mb_compressor_stereo is not installed");
  }

  gconnections.push_back(g_signal_connect(settings, "changed::sidechain-input-device",
                                          G_CALLBACK(+[](GSettings* settings, char* key, gpointer user_data) {
                                            auto self = static_cast<MultibandCompressor*>(user_data);

                                            self->update_sidechain_links(key);
                                          }),
                                          this));

  lv2_wrapper->bind_key_enum(settings, "compressor-mode", "mode");

  lv2_wrapper->bind_key_enum(settings, "envelope-boost", "envb");

  for (uint n = 0U; n < n_bands; n++) {
    const auto nstr = std::to_string(n);

    gconnections.push_back(g_signal_connect(settings, "changed::external-sidechain",
                                            G_CALLBACK(+[](GSettings* settings, char* key, gpointer user_data) {
                                              auto self = static_cast<MultibandCompressor*>(user_data);

                                              self->update_sidechain_links(key);
                                            }),
                                            this));

    lv2_wrapper->bind_key_bool(settings, "external-sidechain" + nstr, "sce_" + nstr);

    if (n > 0U) {
      lv2_wrapper->bind_key_bool(settings, "enable-band" + nstr, "cbe_" + nstr);

      lv2_wrapper->bind_key_double(settings, "split-frequency" + nstr, "sf_" + nstr);
    }

    lv2_wrapper->bind_key_enum(settings, "sidechain-source" + nstr, "scs_" + nstr);

    lv2_wrapper->bind_key_enum(settings, "sidechain-mode" + nstr, "scm_" + nstr);

    lv2_wrapper->bind_key_double(settings, "sidechain-lookahead" + nstr, "sla_" + nstr);

    lv2_wrapper->bind_key_double(settings, "sidechain-reactivity" + nstr, "scr_" + nstr);

    lv2_wrapper->bind_key_double_db(settings, "sidechain-preamp" + nstr, "scp_" + nstr);

    lv2_wrapper->bind_key_bool(settings, "sidechain-custom-lowcut-filter" + nstr, "sclc_" + nstr);

    lv2_wrapper->bind_key_bool(settings, "sidechain-custom-highcut-filter" + nstr, "schc_" + nstr);

    lv2_wrapper->bind_key_double(settings, "sidechain-lowcut-frequency" + nstr, "sclf_" + nstr);

    lv2_wrapper->bind_key_double(settings, "sidechain-highcut-frequency" + nstr, "schf_" + nstr);

    lv2_wrapper->bind_key_enum(settings, "compression-mode" + nstr, "cm_" + nstr);

    lv2_wrapper->bind_key_bool(settings, "compressor-enable" + nstr, "ce_" + nstr);

    lv2_wrapper->bind_key_bool(settings, "solo" + nstr, "bs_" + nstr);

    lv2_wrapper->bind_key_bool(settings, "mute" + nstr, "bm_" + nstr);

    lv2_wrapper->bind_key_double_db(settings, "attack-threshold" + nstr, "al_" + nstr);

    lv2_wrapper->bind_key_double(settings, "attack-time" + nstr, "at_" + nstr);

    lv2_wrapper->bind_key_double_db(settings, "release-threshold" + nstr, "rrl_" + nstr);

    lv2_wrapper->bind_key_double(settings, "release-time" + nstr, "rt_" + nstr);

    lv2_wrapper->bind_key_double(settings, "ratio" + nstr, "cr_" + nstr);

    lv2_wrapper->bind_key_double_db(settings, "knee" + nstr, "kn_" + nstr);

    lv2_wrapper->bind_key_double_db(settings, "boost-threshold" + nstr, "bth_" + nstr);

    lv2_wrapper->bind_key_double_db(settings, "boost-amount" + nstr, "bsa_" + nstr);

    lv2_wrapper->bind_key_double_db(settings, "makeup" + nstr, "mk_" + nstr);
  }

  setup_input_output_gain();
}

MultibandCompressor::~MultibandCompressor() {
  if (connected_to_pw) {
    disconnect_from_pw();
  }

  util::debug(log_tag + name + " destroyed");
}

void MultibandCompressor::setup() {
  if (!lv2_wrapper->found_plugin) {
    return;
  }

  lv2_wrapper->set_n_samples(n_samples);

  if (lv2_wrapper->get_rate() != rate) {
    lv2_wrapper->create_instance(rate);
  }
}

void MultibandCompressor::process(std::span<float>& left_in,
                                  std::span<float>& right_in,
                                  std::span<float>& left_out,
                                  std::span<float>& right_out,
                                  std::span<float>& probe_left,
                                  std::span<float>& probe_right) {
  if (!lv2_wrapper->found_plugin || !lv2_wrapper->has_instance() || bypass) {
    std::copy(left_in.begin(), left_in.end(), left_out.begin());
    std::copy(right_in.begin(), right_in.end(), right_out.begin());

    return;
  }

  if (input_gain != 1.0F) {
    apply_gain(left_in, right_in, input_gain);
  }

  lv2_wrapper->connect_data_ports(left_in, right_in, left_out, right_out, probe_left, probe_right);
  lv2_wrapper->run();

  if (output_gain != 1.0F) {
    apply_gain(left_out, right_out, output_gain);
  }

  /*
   This plugin gives the latency in number of samples
 */

  const auto lv = static_cast<uint>(lv2_wrapper->get_control_port_value("out_latency"));

  if (latency_n_frames != lv) {
    latency_n_frames = lv;

    latency_port_value = static_cast<float>(latency_n_frames) / static_cast<float>(rate);

    util::debug(log_tag + name + " latency: " + std::to_string(latency_port_value) + " s");

    util::idle_add([=, this]() {
      if (!post_messages) {
        return;
      }

      latency.emit(latency_port_value);
    });

    g_idle_add((GSourceFunc) +
                   [](gpointer user_data) {
                     if (!post_messages) {
                       return G_SOURCE_REMOVE;
                     }

                     auto* self = static_cast<MultibandCompressor*>(user_data);

                     if (self->latency.empty()) {
                       return G_SOURCE_REMOVE;
                     }

                     self->latency.emit(self->latency_port_value);

                     return G_SOURCE_REMOVE;
                   },
               this);

    spa_process_latency_info latency_info{};

    latency_info.ns = static_cast<uint64_t>(latency_port_value * 1000000000.0F);

    std::array<char, 1024U> buffer{};

    spa_pod_builder b{};

    spa_pod_builder_init(&b, buffer.data(), sizeof(buffer));

    const spa_pod* param = spa_process_latency_build(&b, SPA_PARAM_ProcessLatency, &latency_info);

    pw_filter_update_params(filter, nullptr, &param, 1);
  }

  if (post_messages) {
    get_peaks(left_in, right_in, left_out, right_out);

    notification_dt += buffer_duration;

    if (notification_dt >= notification_time_window) {
      for (uint n = 0U; n < n_bands; n++) {
        const auto nstr = std::to_string(n);

        frequency_range_end_port_array.at(n) = lv2_wrapper->get_control_port_value("fre_" + nstr);
        envelope_port_array.at(n) = lv2_wrapper->get_control_port_value("elm_" + nstr);
        curve_port_array.at(n) = lv2_wrapper->get_control_port_value("clm_" + nstr);
        reduction_port_array.at(n) = lv2_wrapper->get_control_port_value("rlm_" + nstr);
      }

      g_idle_add((GSourceFunc) +
                     [](gpointer user_data) {
                       if (!post_messages) {
                         return G_SOURCE_REMOVE;
                       }

                       auto* self = static_cast<MultibandCompressor*>(user_data);

                       if (self->frequency_range.empty() || self->envelope.empty() || self->curve.empty() ||
                           self->reduction.empty()) {
                         return G_SOURCE_REMOVE;
                       }

                       self->frequency_range.emit(self->frequency_range_end_port_array);
                       self->envelope.emit(self->envelope_port_array);
                       self->curve.emit(self->curve_port_array);
                       self->reduction.emit(self->reduction_port_array);

                       return G_SOURCE_REMOVE;
                     },
                 this);

      notify();

      notification_dt = 0.0F;
    }
  }
}

void MultibandCompressor::update_sidechain_links(const Glib::ustring& key) {
  auto external_sidechain_enabled = false;

  for (uint n = 0U; !external_sidechain_enabled && n < n_bands; n++) {
    const auto nstr = std::to_string(n);

    external_sidechain_enabled = g_settings_get_boolean(settings, ("external-sidechain" + nstr).c_str()) != 0;
  }

  if (external_sidechain_enabled) {
    const auto device_name = std::string(g_settings_get_string(settings, "sidechain-input-device"));

    NodeInfo input_device = pm->ee_source_node;

    for (const auto& [ts, node] : pm->node_map) {
      if (node.name == device_name) {
        input_device = node;

        break;
      }
    }

    pm->destroy_links(list_proxies);

    list_proxies.clear();

    for (const auto& link : pm->link_nodes(input_device.id, get_node_id(), true)) {
      list_proxies.push_back(link);
    }
  } else {
    pm->destroy_links(list_proxies);

    list_proxies.clear();
  }
}

void MultibandCompressor::update_probe_links() {
  update_sidechain_links("");
}