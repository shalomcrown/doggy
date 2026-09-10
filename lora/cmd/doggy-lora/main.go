package main

import (
	"crypto/tls"
	"flag"
	"io"
	"log"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"runtime"
	"syscall"
	"time"

	"doggy-lora/internal/hop"
	"doggy-lora/internal/radio"
	"doggy-lora/internal/serialport"
	"doggy-lora/internal/settings"
)

// ================================================================================

func main() {
	log.SetFlags(log.LstdFlags)
	mode := flag.String("mode", "robot", "robot or operator")
	listen := flag.String("listen", "127.0.0.1:8765", "operator listen address")
	webRoot := flag.String("web-root", "/usr/share/doggy", "HTML directory")
	configURL := flag.String("config-url", "https://127.0.0.1/api/config", "robot mode config URL")
	doggyConfig := flag.String("doggy-config", "/etc/doggy/doggy.json", "robot file for air_key (not in public GET)")
	localFile := flag.String("config-file", defaultOperatorFile(), "operator lora JSON file")
	serialPath := flag.String("serial", "", "override USB serial device")
	poll := flag.Duration("poll", 5*time.Second, "how often to re-read settings and AT")
	flag.Parse()

	switch *mode {
	case "robot":
		runRobot(*configURL, *doggyConfig, *serialPath, *poll)
	case "operator":
		runOperator(*listen, *webRoot, *localFile, *serialPath, *poll)
	default:
		log.Fatalf("unknown --mode %s", *mode)
	}
}

// ================================================================================

func defaultOperatorFile() string {
	if runtime.GOOS == "windows" {
		if pd := os.Getenv("PROGRAMDATA"); pd != "" {
			return filepath.Join(pd, "doggy", "lora.json")
		}
		return `C:\ProgramData\doggy\lora.json`
	}
	if home, err := os.UserConfigDir(); err == nil && home != "" {
		return filepath.Join(home, "doggy", "lora.json")
	}
	return "/var/lib/doggy-lora/lora.json"
}

// ================================================================================

func runRobot(configURL, doggyConfig, serialOverride string, poll time.Duration) {
	client := &http.Client{
		Timeout: 10 * time.Second,
		Transport: &http.Transport{
			TLSClientConfig: &tls.Config{InsecureSkipVerify: true}, // doggy self-signed
		},
	}
	dev := radio.New(serialport.Open)
	rt := hop.New(hop.RoleRobot, dev, "https://127.0.0.1")
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGINT, syscall.SIGTERM)
	tick := time.NewTicker(poll)
	defer tick.Stop()
	applyOnce := func() {
		desired, err := fetchConfig(client, configURL)
		if err != nil {
			log.Printf("lora config fetch: %v", err)
			return
		}
		if fileS, err := settings.LoadFile(doggyConfig); err == nil {
			desired.AirKey = fileS.AirKey
		}
		if serialOverride != "" {
			desired.Device = serialOverride
		}
		rt.Sync(desired)
	}
	applyOnce()
	for {
		select {
		case <-stop:
			rt.Sync(settings.Settings{Enabled: false})
			return
		case <-tick.C:
			applyOnce()
		}
	}
}

// ================================================================================

func fetchConfig(client *http.Client, url string) (settings.Settings, error) {
	res, err := client.Get(url)
	if err != nil {
		return settings.Settings{}, err
	}
	defer res.Body.Close()
	body, err := io.ReadAll(res.Body)
	if err != nil {
		return settings.Settings{}, err
	}
	return settings.ParseLoraJSON(body)
}
