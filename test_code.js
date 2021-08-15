// Various test functions for websockets and other stuff maybe
function test_websocket_wait_recv(){
  x = new WebSocket(`wss://${window.location.hostname}/ws/test`);
  x.onmessage = (e) => e.data.text().then((data) => console.log(data))
  return x;
}

function test_websocket_send(){
  x = new WebSocket(`wss://${window.location.hostname}/ws/test`);
  x.onmessage = (e) => console.log(e.data)
  x.onopen = () => x.send("Test string");
}