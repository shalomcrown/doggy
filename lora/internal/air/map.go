package air

import (
	"fmt"
	"net/http"
	"strconv"
	"strings"

	"doggy-lora/internal/pb"

	"google.golang.org/protobuf/encoding/protojson"
	"google.golang.org/protobuf/proto"
)

var jsonOpts = protojson.MarshalOptions{UseProtoNames: true}

// ================================================================================

func HTTPToAir(method, path string, body []byte) (*pb.AirRequest, error) {
	path = strings.TrimSpace(path)
	if q := strings.IndexByte(path, '?'); q >= 0 {
		path = path[:q]
	}
	req := &pb.AirRequest{}
	switch {
	case method == http.MethodGet && path == "/api/status":
		req.Op = &pb.AirRequest_GetStatus{GetStatus: &pb.Empty{}}
	case method == http.MethodPost && path == "/api/heartbeat":
		req.Op = &pb.AirRequest_Heartbeat{Heartbeat: &pb.Empty{}}
	case method == http.MethodGet && path == "/api/config":
		req.Op = &pb.AirRequest_GetConfig{GetConfig: &pb.Empty{}}
	case method == http.MethodPut && path == "/api/config":
		cfg := &pb.Config{}
		if err := protojson.Unmarshal(body, cfg); err != nil {
			return nil, err
		}
		req.Op = &pb.AirRequest_PutConfig{PutConfig: cfg}
	case method == http.MethodGet && path == "/api/servos":
		req.Op = &pb.AirRequest_GetServos{GetServos: &pb.Empty{}}
	case method == http.MethodPost && strings.HasPrefix(path, "/api/servos/"):
		id, err := strconv.Atoi(strings.TrimPrefix(path, "/api/servos/"))
		if err != nil {
			return nil, err
		}
		cmd := &pb.ServoCommand{}
		if err := protojson.Unmarshal(body, cmd); err != nil {
			return nil, err
		}
		req.Op = &pb.AirRequest_SetServo{SetServo: &pb.ServoAir{
			Id:      proto.Int32(int32(id)),
			Command: cmd,
		}}
	case method == http.MethodPost && path == "/api/home":
		req.Op = &pb.AirRequest_Home{Home: &pb.Empty{}}
	case method == http.MethodPost && path == "/api/drive":
		drive := &pb.Drive{}
		if err := protojson.Unmarshal(body, drive); err != nil {
			return nil, err
		}
		req.Op = &pb.AirRequest_Drive{Drive: drive}
	case method == http.MethodPost && path == "/api/stop":
		// Represent stop as a Drive with sentinel turn == -1e9 and speed 0
		req.Op = &pb.AirRequest_Drive{Drive: &pb.Drive{Speed: proto.Float64(0.0), Turn: proto.Float64(-1e9)}}
	case method == http.MethodPost && path == "/api/brake":
		// Represent brake as a Drive with sentinel turn == +1e9 and speed 0
		req.Op = &pb.AirRequest_Drive{Drive: &pb.Drive{Speed: proto.Float64(0.0), Turn: proto.Float64(1e9)}}
	case method == http.MethodPost && path == "/api/system":
		cmd := &pb.SystemCommand{}
		if err := protojson.Unmarshal(body, cmd); err != nil {
			return nil, err
		}
		req.Op = &pb.AirRequest_System{System: cmd}
	case method == http.MethodPost && path == "/api/system/pin":
		cmd := &pb.PinCommand{}
		if err := protojson.Unmarshal(body, cmd); err != nil {
			return nil, err
		}
		req.Op = &pb.AirRequest_SetPin{SetPin: cmd}
	default:
		return nil, fmt.Errorf("unsupported air path %s %s", method, path)
	}
	return req, nil
}

// ================================================================================

func AirToHTTP(req *pb.AirRequest) (HttpRequest, error) {
	if req == nil {
		return HttpRequest{}, fmt.Errorf("nil air request")
	}
	switch op := req.Op.(type) {
	case *pb.AirRequest_GetStatus:
		return HttpRequest{Method: http.MethodGet, Path: "/api/status"}, nil
	case *pb.AirRequest_Heartbeat:
		return HttpRequest{Method: http.MethodPost, Path: "/api/heartbeat"}, nil
	case *pb.AirRequest_GetConfig:
		return HttpRequest{Method: http.MethodGet, Path: "/api/config"}, nil
	case *pb.AirRequest_PutConfig:
		return HttpRequest{
			Method:      http.MethodPut,
			Path:        "/api/config",
			ContentType: "application/json",
			Body:        marshalJSON(op.PutConfig),
		}, nil
	case *pb.AirRequest_GetServos:
		return HttpRequest{Method: http.MethodGet, Path: "/api/servos"}, nil
	case *pb.AirRequest_SetServo:
		id := 0
		if op.SetServo != nil {
			id = int(op.SetServo.GetId())
		}
		body := []byte("{}")
		if op.SetServo != nil && op.SetServo.Command != nil {
			body = marshalJSON(op.SetServo.Command)
		}
		return HttpRequest{
			Method:      http.MethodPost,
			Path:        "/api/servos/" + strconv.Itoa(id),
			ContentType: "application/json",
			Body:        body,
		}, nil
	case *pb.AirRequest_Home:
		return HttpRequest{Method: http.MethodPost, Path: "/api/home"}, nil
	case *pb.AirRequest_Drive:
		// Check for sentinel values that encode stop/brake
		if op.Drive != nil {
			if op.Drive.GetTurn() == -1e9 && op.Drive.GetSpeed() == 0.0 {
				return HttpRequest{Method: http.MethodPost, Path: "/api/stop"}, nil
			}
			if op.Drive.GetTurn() == 1e9 && op.Drive.GetSpeed() == 0.0 {
				return HttpRequest{Method: http.MethodPost, Path: "/api/brake"}, nil
			}
		}
		return HttpRequest{
			Method:      http.MethodPost,
			Path:        "/api/drive",
			ContentType: "application/json",
			Body:        marshalJSON(op.Drive),
		}, nil
	case *pb.AirRequest_System:
		return HttpRequest{
			Method:      http.MethodPost,
			Path:        "/api/system",
			ContentType: "application/json",
			Body:        marshalJSON(op.System),
		}, nil
	case *pb.AirRequest_SetPin:
		return HttpRequest{
			Method:      http.MethodPost,
			Path:        "/api/system/pin",
			ContentType: "application/json",
			Body:        marshalJSON(op.SetPin),
		}, nil
	default:
		return HttpRequest{}, fmt.Errorf("unsupported air op")
	}
}

// ================================================================================

func HTTPToAirResponse(req *pb.AirRequest, resp HttpResponse) *pb.AirResponse {
	out := &pb.AirResponse{
		Status:      proto.Int32(int32(resp.Status)),
		ContentType: proto.String("application/json"),
	}
	if resp.Status >= 400 {
		errMsg := &pb.ApiError{}
		if protojson.Unmarshal(resp.Body, errMsg) != nil || errMsg.GetError() == "" {
			errMsg.Error = proto.String("http_error")
		}
		out.Payload = &pb.AirResponse_Error{Error: errMsg}
		return out
	}
	switch req.GetOp().(type) {
	case *pb.AirRequest_GetStatus, *pb.AirRequest_Heartbeat:
		st := &pb.Status{}
		_ = protojson.Unmarshal(resp.Body, st)
		out.Payload = &pb.AirResponse_StatusBody{StatusBody: st}
	case *pb.AirRequest_GetConfig, *pb.AirRequest_PutConfig:
		cfg := &pb.Config{}
		_ = protojson.Unmarshal(resp.Body, cfg)
		out.Payload = &pb.AirResponse_Config{Config: cfg}
	case *pb.AirRequest_GetServos, *pb.AirRequest_SetServo, *pb.AirRequest_Home:
		list := &pb.ServoList{}
		_ = protojson.Unmarshal(resp.Body, list)
		out.Payload = &pb.AirResponse_Servos{Servos: list}
	case *pb.AirRequest_Drive:
		st := &pb.Status{}
		_ = protojson.Unmarshal(resp.Body, st)
		out.Payload = &pb.AirResponse_StatusBody{StatusBody: st}
	case *pb.AirRequest_System:
		acc := &pb.SystemAccepted{}
		_ = protojson.Unmarshal(resp.Body, acc)
		out.Payload = &pb.AirResponse_SystemAccepted{SystemAccepted: acc}
	case *pb.AirRequest_SetPin:
		cfg := &pb.Config{}
		_ = protojson.Unmarshal(resp.Body, cfg)
		out.Payload = &pb.AirResponse_Config{Config: cfg}
	}
	return out
}

// ================================================================================

func AirHTTP(resp *pb.AirResponse) (status int, contentType string, body []byte) {
	if resp == nil {
		return http.StatusBadGateway, "application/json", []byte(`{"error":"empty_air"}`)
	}
	status = int(resp.GetStatus())
	if status == 0 {
		status = http.StatusOK
	}
	contentType = resp.GetContentType()
	if contentType == "" {
		contentType = "application/json"
	}
	switch p := resp.Payload.(type) {
	case *pb.AirResponse_StatusBody:
		body = marshalJSON(p.StatusBody)
	case *pb.AirResponse_Config:
		body = marshalJSON(p.Config)
	case *pb.AirResponse_Servos:
		body = marshalJSON(p.Servos)
	case *pb.AirResponse_SystemAccepted:
		body = marshalJSON(p.SystemAccepted)
	case *pb.AirResponse_Error:
		body = marshalJSON(p.Error)
	default:
		body = []byte{}
	}
	return status, contentType, body
}

// ================================================================================

func marshalJSON(m proto.Message) []byte {
	if m == nil {
		return []byte("{}")
	}
	b, err := jsonOpts.Marshal(m)
	if err != nil {
		return []byte(`{"error":"marshal"}`)
	}
	return b
}
