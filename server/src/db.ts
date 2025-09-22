// db.ts
import pkg from "pg";
import dotenv from "dotenv";
import bcrypt from "bcrypt";

dotenv.config();

const { Pool } = pkg;

// === CONFIG from .env ===
const dbName = process.env.DB_NAME || "smart_dorm_parcel";
const dbUser = process.env.DB_USER || "athip";
const dbPassword = process.env.DB_PASSWORD || "123456";
const dbHost = process.env.DB_HOST || "localhost";
const dbPort = process.env.DB_PORT ? Number(process.env.DB_PORT) : 5432;

// Connect to default postgres (for DB creation check)
const adminPool = new Pool({
  user: dbUser,
  password: dbPassword,
  host: dbHost,
  port: dbPort,
  database: "postgres", // system DB
});

// Create database if not exists
async function ensureDatabase() {
  try {
    const checkDb = await adminPool.query(
      `SELECT 1 FROM pg_database WHERE datname = $1`,
      [dbName]
    );

    if (checkDb.rowCount === 0) {
      await adminPool.query(`CREATE DATABASE ${dbName}`);
      console.log(`✅ Database "${dbName}" created`);
    } else {
      console.log(`ℹ️ Database "${dbName}" already exists`);
    }
  } catch (err) {
    console.error("❌ Error ensuring database:", err);
    throw err;
  }
}

// === POOL for target DB ===
let pool: pkg.Pool;

async function initDB() {
  await ensureDatabase();

  pool = new Pool({
    user: dbUser,
    password: dbPassword,
    host: dbHost,
    port: dbPort,
    database: dbName,
  });

  // === CREATE TABLES ===
  const createUsersTable = `
    CREATE TABLE IF NOT EXISTS users (
      id SERIAL PRIMARY KEY,
      username VARCHAR(255) UNIQUE NOT NULL,
      password VARCHAR(255) NOT NULL,
      dorm_number VARCHAR(50) NOT NULL
    );
  `;

  const createTrackingTable = `
    CREATE TABLE IF NOT EXISTS tracking_numbers (
      id SERIAL PRIMARY KEY,
      user_id INTEGER NOT NULL,
      tracking_number VARCHAR(255) NOT NULL,
      status VARCHAR(50) DEFAULT 'pending',
      created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
      CONSTRAINT unique_user_tracking UNIQUE (user_id, tracking_number) -- กัน user เดียวกันเพิ่มเลขซ้ำ
    );
  `;

  try {
    await pool.query(createUsersTable);
    console.log("✅ Users table ready");

    await pool.query(createTrackingTable);
    console.log("✅ Tracking numbers table ready");

    // Create admin user after tables are ready and pool is initialized
    await createAdmin(pool);
  } catch (err) {
    console.error("❌ Error creating tables:", err);
  }
}

(async () => {
  await initDB();
})();

// === QUERIES ===
export async function createUser(
  username: string,
  password: string,
  dormNumber: string
) {
  const query = `
    INSERT INTO users (username, password, dorm_number)
    VALUES ($1, $2, $3)
    RETURNING id, username, dorm_number
  `;
  const result = await pool.query(query, [username, password, dormNumber]);
  return result.rows[0];
}


// === CREATE ADMIN USER IF NOT EXISTS ===
async function createAdmin(pool: pkg.Pool) {
  const username = "admin";
  const password = "admin"; // plaintext
  const dorm = "0"; // admin ไม่ต้องมีหอ

  const hashedPassword = await bcrypt.hash(password, 10);

  const check = await pool.query("SELECT * FROM users WHERE username=$1", [username]);
  if (check.rows.length === 0) {
    await pool.query(
      "INSERT INTO users (username, password, dorm_number) VALUES ($1, $2, $3)",
      [username, hashedPassword, dorm]
    );
    console.log("✅ Admin user created (username=admin, password=admin)");
  } else {
    console.log("ℹ️ Admin user already exists");
  }
}

export async function getUserByUsername(username: string) {
  const query = `SELECT * FROM users WHERE username = $1`;
  const result = await pool.query(query, [username]);
  return result.rows[0];
}

export async function addTrackingNumber(
  userId: number,
  trackingNumber: string
) {
  try {
    const query = `
      INSERT INTO tracking_numbers (user_id, tracking_number)
      VALUES ($1, $2)
      RETURNING id, tracking_number, status, created_at
    `;
    const result = await pool.query(query, [userId, trackingNumber]);
    return result.rows[0];
  } catch (err: any) {
    if (err.code === "23505") {
      // 23505 = unique_violation
      throw new Error("Tracking number already exists");
    }
    throw err;
  }
}


export async function getTrackingNumbersByUser(userId: number) {
  const query = `
    SELECT tracking_number, status, created_at
    FROM tracking_numbers
    WHERE user_id = $1
    ORDER BY created_at DESC
  `;
  const result = await pool.query(query, [userId]);
  return result.rows;
}

export async function deleteTrackingNumber(trackingNumber: string) {
  const query = `
    DELETE FROM tracking_numbers
    WHERE tracking_number = $1
    RETURNING id, tracking_number, status, created_at
  `;
  const result = await pool.query(query, [trackingNumber]);
  return result.rows[0];
}

// === NEW FUNCTIONS ===

// Read status
export async function getTrackingStatus(trackingNumber: string) {
  const query = `
    SELECT status
    FROM tracking_numbers
    WHERE tracking_number = $1
  `;
  const result = await pool.query(query, [trackingNumber]);
  return result.rows[0]?.status;
}

// Update status
export async function updateTrackingStatus(
  trackingNumber: string,
  status: string
) {
  const query = `
    UPDATE tracking_numbers
    SET status = $2
    WHERE tracking_number = $1
    RETURNING id, tracking_number, status, created_at
  `;
  const result = await pool.query(query, [trackingNumber, status]);
  return result.rows[0];
}

export async function getDormInfoByTrackingNumber(trackingNumber: string) {
  const query = `
    SELECT u.username, u.dorm_number
    FROM tracking_numbers tn
    JOIN users u ON tn.user_id = u.id
    WHERE tn.tracking_number = $1
    LIMIT 1
  `;
  const result = await pool.query(query, [trackingNumber]);
  return result.rows[0]; // { username, dorm_number } or undefined
}

export async function getAllUsers() {
  const query = `SELECT id, username, dorm_number, role FROM users ORDER BY id`;
  const result = await pool.query(query);
  return result.rows;
}

export async function getAllTracking() {
  const query = `
    SELECT tn.id, tn.tracking_number, tn.status, tn.created_at, u.username, u.dorm_number
    FROM tracking_numbers tn
    JOIN users u ON tn.user_id = u.id
    ORDER BY tn.created_at DESC
  `;
  const result = await pool.query(query);
  return result.rows;
}

export async function getUserById(id: number) {
  const query = `SELECT * FROM users WHERE id = $1`;
  const result = await pool.query(query, [id]);
  return result.rows[0];
}



export { pool };
