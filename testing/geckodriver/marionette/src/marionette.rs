use serde::{Deserialize, Serialize};

use crate::common::BoolValue;

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub enum Command {
    #[serde(rename = "Marionette:AcceptConnections")]
    AcceptConnections(BoolValue),
    #[serde(rename = "Marionette:GetContext")]
    GetContext,
    #[serde(rename = "Marionette:GetScreenOrientation")]
    GetScreenOrientation,
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test::assert_ser_de;
    use serde_json::json;

    #[test]
    fn test_json_command_accept_connections() {
        assert_ser_de(
            &Command::AcceptConnections(BoolValue::new(false)),
            json!({"Marionette:AcceptConnections": {"value": false }}),
        );
    }

    #[test]
    fn test_json_command_get_context() {
        assert_ser_de(&Command::GetContext, json!("Marionette:GetContext"));
    }

    #[test]
    fn test_json_command_get_screen_orientation() {
        assert_ser_de(
            &Command::GetScreenOrientation,
            json!("Marionette:GetScreenOrientation"),
        );
    }

    #[test]
    fn test_json_command_invalid() {
        assert!(serde_json::from_value::<Command>(json!("foo")).is_err());
    }
}
