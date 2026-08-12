package com.qcode.android;

import android.content.SharedPreferences;
import android.os.Bundle;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;

public class SettingsActivity extends AppCompatActivity {
    private EditText etAnthropicKey;
    private EditText etGroqKey;
    private EditText etOllamaHost;
    private EditText etOpenAiKey;
    private EditText etOpenCodeKey;
    private EditText etOpenRouterKey;
    private EditText etWorkspaceDir;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_settings);
        etOpenRouterKey = findViewById(R.id.etOpenRouterKey);
        etOpenCodeKey = findViewById(R.id.etOpenCodeKey);
        etOpenAiKey = findViewById(R.id.etOpenAiKey);
        etAnthropicKey = findViewById(R.id.etAnthropicKey);
        etGroqKey = findViewById(R.id.etGroqKey);
        etOllamaHost = findViewById(R.id.etOllamaHost);
        etWorkspaceDir = findViewById(R.id.etWorkspaceDir);
        Button btnSave = findViewById(R.id.btnSave);
        loadSettings();
        btnSave.setOnClickListener(v -> {
            saveSettings();
            Toast.makeText(this, "Settings Saved!", Toast.LENGTH_SHORT).show();
            finish();
        });
    }

    private void loadSettings() {
        SharedPreferences prefs = getSharedPreferences("qcode_settings", 0);
        etOpenRouterKey.setText(prefs.getString("OPENROUTER_API_KEY", QCodeBridge.DEFAULT_OPENROUTER_KEY));
        etOpenCodeKey.setText(prefs.getString("OPENCODE_API_KEY", ""));
        etOpenAiKey.setText(prefs.getString("OPENAI_API_KEY", ""));
        etAnthropicKey.setText(prefs.getString("ANTHROPIC_API_KEY", ""));
        etGroqKey.setText(prefs.getString("GROQ_API_KEY", ""));
        etOllamaHost.setText(prefs.getString("OLLAMA_HOST", "http://localhost:11434"));
        etWorkspaceDir.setText(prefs.getString("WORKSPACE_DIR",
                getApplicationContext().getFilesDir().getAbsolutePath()));
    }

    private void saveSettings() {
        String openrouterKey = etOpenRouterKey.getText().toString().trim();
        String opencodeKey = etOpenCodeKey.getText().toString().trim();
        String openaiKey = etOpenAiKey.getText().toString().trim();
        String anthropicKey = etAnthropicKey.getText().toString().trim();
        String groqKey = etGroqKey.getText().toString().trim();
        String ollamaHost = etOllamaHost.getText().toString().trim();
        String workspaceDir = etWorkspaceDir.getText().toString().trim();
        getSharedPreferences("qcode_settings", 0).edit()
                .putString("OPENROUTER_API_KEY", openrouterKey)
                .putString("OPENCODE_API_KEY", opencodeKey)
                .putString("OPENAI_API_KEY", openaiKey)
                .putString("ANTHROPIC_API_KEY", anthropicKey)
                .putString("GROQ_API_KEY", groqKey)
                .putString("OLLAMA_HOST", ollamaHost)
                .putString("WORKSPACE_DIR", workspaceDir)
                .apply();
        QCodeBridge.saveEnvironmentKeys(openrouterKey, openaiKey, anthropicKey, groqKey);
        QCodeBridge.writeProviderConfig(getApplicationContext());
    }
}
