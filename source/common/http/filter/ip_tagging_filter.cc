#include "common/http/filter/ip_tagging_filter.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Http {

IpTaggingFilter::IpTaggingFilter(IpTaggingFilterConfigSharedPtr config) : config_(config) {}

IpTaggingFilter::~IpTaggingFilter() {}

void IpTaggingFilter::onDestroy() {}

FilterHeadersStatus IpTaggingFilter::decodeHeaders(HeaderMap& headers, bool) {
  bool is_internal_request =
      headers.EnvoyInternalRequest() && (headers.EnvoyInternalRequest()->value() == "true");

  if ((is_internal_request && config_->requestType() == FilterRequestType::EXTERNAL) ||
      (!is_internal_request && config_->requestType() == FilterRequestType::INTERNAL) ||
      (!config_->runtime().snapshot().featureEnabled("ip_tagging.http_filter_enabled", 100))) {
    return FilterHeadersStatus::Continue;
  }

  std::vector<std::string> tags =
      config_->trie().getTags(callbacks_->requestInfo().downstreamRemoteAddress());

  if (!tags.empty()) {
    HeaderString& header = headers.insertEnvoyIpTags().value();
    if (!header.empty()) {
      header.append(", ", 2);
    }

    // When there are multiple tags, they are joined into one string prior to setting
    // the header.
    if (tags.size() > 1) {
      const std::string tags_join = absl::StrJoin(tags, ", ");
      header.append(tags_join.c_str(), tags_join.size());
    } else {
      header.append(tags[0].c_str(), tags[0].size());
    }

    // For a large number(ex > 1000) of tags, stats cardinality will be an issue.
    // If there are use cases with a large set of tags, a way to opt into these stats
    // should be exposed and other observability options like logging tags need to be implemented.
    for (const std::string& tag : tags) {
      config_->scope().counter(fmt::format("{}{}.hit", config_->statsPrefix(), tag)).inc();
    }
  } else {
    config_->scope().counter(fmt::format("{}no_hit", config_->statsPrefix())).inc();
  }
  config_->scope().counter(fmt::format("{}total", config_->statsPrefix())).inc();
  return FilterHeadersStatus::Continue;
}

FilterDataStatus IpTaggingFilter::decodeData(Buffer::Instance&, bool) {
  return FilterDataStatus::Continue;
}

FilterTrailersStatus IpTaggingFilter::decodeTrailers(HeaderMap&) {
  return FilterTrailersStatus::Continue;
}

void IpTaggingFilter::setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

} // namespace Http
} // namespace Envoy
