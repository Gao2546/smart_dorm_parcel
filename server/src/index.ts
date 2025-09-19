// server.ts
import express from "express";
import dotenv from "dotenv";
import cors from "cors";
import path from "path";
import session from "express-session";

import {
  createUser,
  getUserByUsername,
  addTrackingNumber,
  getTrackingNumbersByUser,
  deleteTrackingNumber, // <-- import the function
  getTrackingStatus,
  updateTrackingStatus,
  getDormInfoByTrackingNumber,
} from "./db.js";
// import { use } from "react";

// Extend express-session types to include userId & darkMode
declare module "express-session" {
  interface SessionData {
    userId?: number;
    darkMode?: boolean;
  }
}


dotenv.config();
const app = express();

// === MIDDLEWARE ===
app.use(cors());
app.use(express.json());
app.use(express.urlencoded({ extended: true }));

// === SESSION SETUP ===
app.use(
  session({
    secret: process.env.SESSION_SECRET || "supersecretkey",
    resave: false,
    saveUninitialized: false,
    cookie: { maxAge: 1000 * 60 * 60 * 24 }, // 1 day
  })
);

// === VIEW ENGINE ===
// app.set("view engine", "ejs");
// app.set("views", path.join(process.cwd(), "src", "views"));

// === STATIC FILES ===
app.use(express.static(path.join(process.cwd(), "public")));

// === LOGIN CHECK MIDDLEWARE ===
function requireLogin(req: any, res: any, next: any) {
  console.log("Session data:", req.session);
  if (!req.session.userId) {
    console.log("User not logged in, redirecting to /login");
    return res.redirect("/login");
  }
  next();
}

// === PAGES ===
app.get("/", requireLogin, (req, res) => {
  res.sendFile(path.join(__dirname, "..", "public", "index.html"));
});

app.get("/register", (req, res) => {
  res.sendFile(path.join(__dirname, "..", "public", "register.html"));
});

app.get("/login", (req, res) => {
  res.sendFile(path.join(__dirname, "..", "public", "login.html"));
});

app.get("/tracking", requireLogin, (req, res) => {
  res.sendFile(path.join(__dirname, "..", "public", "tracking.html"));
});

// === REGISTER (API) ===
app.post("/register", async (req, res) => {
  const { username, password, dorm } = req.body;

  if (!username || !password || !dorm) {
    return res.status(400).json({ error: "Missing fields" });
  }

  try {
    const user = await createUser(username, password, dorm);
    res.status(201).json({ message: "User registered", user });
  } catch (err: any) {
    console.error("Register error:", err);
    res.status(400).json({ error: err.message });
  }
});

// === LOGIN (API) ===
app.post("/login", async (req, res) => {
  const { username, password } = req.body;

  if (!username || !password) {
    return res.status(400).json({ error: "Missing fields" });
  }

  try {
    const user = await getUserByUsername(username);

    if (!user) {
      return res.status(401).json({ error: "User not found" });
    }

    if (user.password !== password) {
      return res.status(401).json({ error: "Invalid password" });
    }

    // ✅ Set session
    req.session.userId = user.id;

    res.json({
      message: "Login successful",
      user: { id: user.id, username: user.username, dorm: user.dorm_number },
    });
  } catch (err: any) {
    console.error("Login error:", err);
    res.status(500).json({ error: err.message });
  }
});

// === LOGOUT ===
app.post("/logout", (req, res) => {
  req.session.destroy((err) => {
    if (err) {
      console.error("Logout error:", err);
      return res.status(500).json({ error: "Could not log out" });
    }
    res.clearCookie("connect.sid");
    res.json({ message: "Logged out" });
  });
});

// === ADD TRACKING NUMBER (API) ===
app.post("/tracking", requireLogin, async (req, res) => {
  const userId = req.session.userId;
  const { trackingNumber } = req.body;
  console.log("User ID from session:", userId);
  console.log("Tracking Number from body:", trackingNumber);

  if (!userId || !trackingNumber) {
    return res.status(400).json({ error: "Missing fields" });
  }

  try {
    const tracking = await addTrackingNumber(userId, trackingNumber);
    res.status(201).json({ message: "Tracking added", tracking });
  } catch (err: any) {
    console.error("Tracking error:", err);
    res.status(400).json({ error: err.message });
  }
});

// === GET USER’S TRACKING NUMBERS (API) ===
app.post("/tracking_number", requireLogin, async (req, res) => {
  const { userId } = req.body;

  if (!userId || isNaN(Number(userId))) {
    return res.status(400).json({ error: "Invalid or missing user ID" });
  }

  try {
    const numbers = await getTrackingNumbersByUser(Number(userId));
    res.json({ trackingNumbers: numbers });
  } catch (err: any) {
    console.error("Fetch tracking error:", err);
    res.status(500).json({ error: err.message });
  }
});


app.get("/me", requireLogin, (req, res) => {
  res.json({ user: { id: req.session.userId } });
});

// === LOGOUT ===
// No requireLogin middleware here, so it can be accessed even with a logged-out session
app.post("/logout", (req, res) => {
  req.session.destroy((err) => {
    if (err) {
      console.error("Logout error:", err);
      return res.status(500).json({ error: "Could not log out" });
    }
    res.clearCookie("connect.sid");
    res.json({ message: "Logged out" });
  });
});

// === DELETE TRACKING NUMBER (API) ===
app.delete("/tracking", requireLogin, async (req, res) => {
  const { trackingNumber } = req.body;

  if (!trackingNumber) {
    return res.status(400).json({ error: "Missing tracking number" });
  }

  try {
    const deleted = await deleteTrackingNumber(trackingNumber);
    if (!deleted) {
      return res.status(404).json({ error: "Tracking number not found" });
    }
    res.json({ message: "Tracking number deleted", deleted });
  } catch (err: any) {
    console.error("Delete tracking error:", err);
    res.status(500).json({ error: err.message });
  }
});

// === SET DARK MODE PREFERENCE ===
app.post("/darkmode", requireLogin, (req, res) => {
  const { enabled } = req.body;

  if (typeof enabled !== "boolean") {
    return res.status(400).json({ error: "Invalid dark mode value" });
  }

  req.session.darkMode = enabled;
  res.json({ message: "Dark mode preference saved", darkMode: enabled });
});

// === GET DARK MODE PREFERENCE ===
app.get("/darkmode", requireLogin, (req, res) => {
  res.json({ darkMode: req.session.darkMode || false });
});


// === GET TRACKING STATUS (POST) ===
app.post("/tracking/status", requireLogin, async (req, res) => {
  const { trackingNumber } = req.body;

  if (!trackingNumber) {
    return res.status(400).json({ error: "Missing tracking number" });
  }

  try {
    const tracking = await getTrackingStatus(trackingNumber); // implement this in db.ts
    if (!tracking) {
      return res.status(404).json({ error: "Tracking number not found" });
    }
    res.json({ trackingNumber, status: tracking.status });
  } catch (err: any) {
    console.error("Fetch tracking status error:", err);
    res.status(500).json({ error: err.message });
  }
});

// === UPDATE TRACKING STATUS (POST) ===
app.post("/tracking/status/update" , async (req, res) => {
  const { trackingNumber, status } = req.body;

  if (!trackingNumber || !status) {
    return res.status(400).json({ error: "Missing fields" });
  }

  try {
    const updated = await updateTrackingStatus(trackingNumber, status); // implement this in db.ts
    if (!updated) {
      return res.status(404).json({ error: "Tracking number not found" });
    }
    res.json({ message: "Status updated", trackingNumber, status: updated.status });
  } catch (err: any) {
    console.error("Update tracking status error:", err);
    res.status(500).json({ error: err.message });
  }
});


// Call Python QR service
async function callPythonReadQR(): Promise<{ qr_text: string }> {
  const res = await fetch("http://127.0.0.1:5000/readQR", {
    method: "POST",
  });
  if (!res.ok) throw new Error(`Python server error: ${res.statusText}`);
  return (await res.json()) as { qr_text: string }; // { qr_text: "..." }
}

app.post("/readQR", async (req, res) => {
  try {
    const { qr_text } = await callPythonReadQR();

    let mapped_label : number;
    if (qr_text && qr_text !== "No QR code detected") {
      const status = await getTrackingStatus(qr_text);
      if (status) {
        await updateTrackingStatus(qr_text, "scanned");

        const dormInfo = await getDormInfoByTrackingNumber(qr_text);
        if (dormInfo) {
          mapped_label = parseInt(dormInfo.dorm_number);
        }
        else{
          mapped_label = -1;
        }
      }
      else{
        mapped_label = -1;
      }
    } else{
      mapped_label = -2;
    }

    res.json({
      message: "QR read successfully!",
      qr_text,
      mapped_label,
    });
  } catch (err: any) {
    console.error("Error:", err);
    res.status(500).json({ error: err.message });
  }
});



// === START SERVER ===
const PORT = process.env.PORT || 3000;
app.listen(PORT, () =>
  console.log(`🚀 Server running at http://localhost:${PORT}`)
);
