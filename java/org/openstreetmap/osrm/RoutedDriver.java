package org.openstreetmap.osrm;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.MalformedURLException;
import java.net.URL;
import java.net.URLConnection;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;

import org.apache.commons.lang3.StringUtils;

/**
* <p>
* Invokes a remote OSRM-routed via HTTP
* </p>
* <p>
* -- For Japanese-native developers: should be consistent with the above -- <br/>
* -- 日本語のドキュメント: 上に書いてあることと整合しているべし。 -- <br/>
* HTTP でリモートの OSRM-routed を呼ぶ
* </p>
*
* @since 2013.01.18
* @version 1.0.0
* @author aquavit
* @license Apache 2.0
*/
public class RoutedDriver {
	public static class Node {
		private final double lon_x, lat_y;
		public Node(double x, double y) {
			lon_x=x;
			lat_y=y;
		}
		public String toString() {
			return "loc=" + lat_y + "," + lon_x;
		}
	}
	
	private static String invoke(String host, int port, String cmd, Collection<Node> targets) throws IOException {
		List<String> coords = new ArrayList<String>(targets.size());
		for (Node n : targets) {
			coords.add(n.toString());
		}
		String reqstr = "/" + cmd + "?" + StringUtils.join(coords, "&");
		URL url = new URL("http", host, port, reqstr);
		HttpURLConnection con = (HttpURLConnection)url.openConnection();
		con.setUseCaches(false);
		con.setRequestMethod("GET");
		BufferedReader br = new BufferedReader(new InputStreamReader(con.getInputStream(), "utf-8"));
		StringBuilder buf = new StringBuilder();
		String line;
		while ( null != ( line = br.readLine() ) ) {
			buf.append(line);
		}
		br.close();
		con.disconnect();
		return buf.toString();
	}
	
	public static String route(String host, int port, Collection<Node> targets) throws IOException {
		return invoke(host, port, "viaroute", targets);
	}
	
	public static String distanceMatrix(String host, int port, Collection<Node> targets) throws IOException {
		return invoke(host, port, "distmatrix", targets);
	}
	
	public static void main(String[] args) {
		if (args.length < 2)
			return;
		List<Node> targets = new ArrayList<Node>();
		for (String arg : args) {
			String[] pair = arg.split(",", 2);
			if (pair.length < 2)
				continue;
			double lat = Double.parseDouble(pair[0]);
			double lon = Double.parseDouble(pair[1]);
			targets.add(new Node(lon, lat));
		}
		try {
			System.out.println(route("localhost", 5000, targets));
			System.out.println(distanceMatrix("localhost", 5000, targets));
		} catch (IOException e) {
			// TODO 自動生成された catch ブロック
			e.printStackTrace();
		}
	}
}
