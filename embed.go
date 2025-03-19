//go:build EMBED_DEPS && darwin
// +build EMBED_DEPS,darwin

package lilliput

import "embed"

//go:embed deps/linux/*
//go:embed deps/osx/*
//go:embed icc_profiles/*
var  embed.FS