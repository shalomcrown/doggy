package air

// ================================================================================

type HttpRequest struct {
	Method      string
	Path        string
	ContentType string
	Body        []byte
}

// ================================================================================

type HttpResponse struct {
	Status      int
	ContentType string
	Body        []byte
}
