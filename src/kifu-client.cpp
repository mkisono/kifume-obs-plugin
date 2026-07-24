#include "kifu-client.h"

#include <cstdio>
#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include <grpcpp/grpcpp.h>

#include "kifume/inference/v1/inference.grpc.pb.h"

struct kifu_client {
	std::string backend_address;
	std::string last_error;
	bool ready = false;
	std::shared_ptr<grpc::Channel> channel;
	std::unique_ptr<kifume::inference::v1::InferenceService::Stub> stub;
};

static constexpr int64_t KIFU_CONNECT_TIMEOUT_MS = 500;

static void CopyError(char *destination, size_t destination_size, const std::string &message)
{
	if (destination == nullptr || destination_size == 0U) {
		return;
	}

	std::snprintf(destination, destination_size, "%s", message.c_str());
}

struct kifu_client *kifu_client_create(const char *backend_address)
{
	auto *client = new kifu_client();
	client->backend_address = backend_address != nullptr ? backend_address : "";

	client->channel = grpc::CreateChannel(client->backend_address, grpc::InsecureChannelCredentials());
	client->stub = kifume::inference::v1::InferenceService::NewStub(client->channel);
	if (client->stub == nullptr) {
		client->ready = false;
		client->last_error = "failed to create gRPC stub";
		return client;
	}

	client->ready = client->channel != nullptr &&
			client->channel->WaitForConnected(
				std::chrono::system_clock::now() + std::chrono::milliseconds(KIFU_CONNECT_TIMEOUT_MS));
	if (!client->ready) {
		client->last_error = "backend is not reachable: " + client->backend_address;
	}

	return client;
}

void kifu_client_destroy(struct kifu_client *client)
{
	delete client;
}

bool kifu_client_is_ready(const struct kifu_client *client)
{
	return client != nullptr && client->ready;
}

const char *kifu_client_last_error(const struct kifu_client *client)
{
	if (client == nullptr) {
		return "client is null";
	}

	return client->last_error.c_str();
}

bool kifu_client_submit_frame(struct kifu_client *client,
			      const struct kifu_submit_request *request,
			      struct kifu_submit_result *response)
{
	if (response != nullptr) {
		response->status = KIFU_RESULT_STATUS_ERROR;
		response->processed_time_unix_ms = 0;
		response->detections_count = 0;
		response->dice_count = 0;
		for (size_t i = 0; i < KIFU_MAX_DICE_RESULTS; ++i) {
			response->dice[i].confidence = 0.0F;
			response->dice[i].box.x = 0.0F;
			response->dice[i].box.y = 0.0F;
			response->dice[i].box.width = 0.0F;
			response->dice[i].box.height = 0.0F;
			response->dice[i].center_x = 0.0F;
			response->dice[i].center_y = 0.0F;
		}
		response->error_message[0] = '\0';
	}

	if (client == nullptr || request == nullptr || response == nullptr) {
		return false;
	}

	if (!client->ready || client->stub == nullptr) {
		client->last_error = "gRPC client is not ready";
		CopyError(response->error_message, sizeof(response->error_message), client->last_error);
		return false;
	}

	auto grpc_request = kifume::inference::v1::SubmitFrameRequest();
	grpc_request.set_request_id(request->request_id != nullptr ? request->request_id : "");
	grpc_request.set_source_id(request->source_id != nullptr ? request->source_id : "");
	grpc_request.set_capture_time_unix_ms(request->capture_time_unix_ms);
	grpc_request.set_allow_stale_result(request->allow_stale_result);
	grpc_request.set_width(request->width);
	grpc_request.set_height(request->height);
	grpc_request.set_frame_data(request->frame_data, static_cast<int>(request->frame_data_size));
	grpc_request.set_frame_format(static_cast<kifume::inference::v1::FrameFormat>(request->frame_format));

	grpc::ClientContext context;
	if (request->timeout_ms > 0U) {
		context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(request->timeout_ms));
	}

	auto grpc_response = kifume::inference::v1::SubmitFrameResponse();
	const grpc::Status status = client->stub->SubmitFrame(&context, grpc_request, &grpc_response);
	if (!status.ok()) {
		client->last_error = status.error_message();
		if (client->last_error.empty()) {
			client->last_error = "SubmitFrame RPC failed";
		} else {
			client->last_error += " (code=" + std::to_string(static_cast<int>(status.error_code())) + ")";
		}
		CopyError(response->error_message, sizeof(response->error_message), client->last_error);
		response->status = KIFU_RESULT_STATUS_ERROR;
		return false;
	}

	response->status = static_cast<kifu_result_status_t>(grpc_response.status());
	response->processed_time_unix_ms = grpc_response.processed_time_unix_ms();
	response->detections_count = static_cast<uint32_t>(grpc_response.detections_size());
	response->dice_count = static_cast<uint32_t>(grpc_response.dice_size());
	if (response->dice_count > KIFU_MAX_DICE_RESULTS) {
		response->dice_count = KIFU_MAX_DICE_RESULTS;
	}

	for (uint32_t i = 0; i < response->dice_count; ++i) {
		const auto &grpc_dice = grpc_response.dice(static_cast<int>(i));
		response->dice[i].confidence = grpc_dice.confidence();
		response->dice[i].box.x = grpc_dice.box().x();
		response->dice[i].box.y = grpc_dice.box().y();
		response->dice[i].box.width = grpc_dice.box().width();
		response->dice[i].box.height = grpc_dice.box().height();
		response->dice[i].center_x = grpc_dice.center().x();
		response->dice[i].center_y = grpc_dice.center().y();
	}

	CopyError(response->error_message, sizeof(response->error_message), grpc_response.error_message());
	client->last_error.clear();
	return true;
}